#ifdef PX_HAS_STEAM

#include "core/downloads/steam.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <QCoro/QCoroFuture>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QtConcurrent>
#include <spdlog/spdlog.h>

#include "core/utils.hpp"

namespace px::modrim {
namespace {

constexpr auto kProgressLogInterval = std::chrono::seconds(15);
constexpr auto kProgressEmitInterval = std::chrono::milliseconds(100);

// Steam item ids are uint64; anything larger is not an item.
std::optional<std::uint64_t> parse_item_id(const QString& text) {
    if (!isAsciiDigits(text)) {
        return std::nullopt;
    }
    const std::string utf8 = text.toStdString();
    std::uint64_t value = 0;
    const char* const last = utf8.data() + utf8.size();
    const auto [end, ec] = std::from_chars(utf8.data(), last, value);
    if (ec != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

QString item_id_text(std::uint64_t id) { return QString::number(static_cast<qulonglong>(id)); }

double unix_now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Settings used to live in config.json under `workshop_<field>` keys.
QHash<QString, QString> legacy_settings_keys() {
    QHash<QString, QString> keys;
    for (const char* field : {"auto_update_hours", "parallel_items", "threads_per_item", "proxy",
                              "connect_timeout", "stall_timeout"}) {
        keys.insert(QStringLiteral("workshop_") + QString::fromLatin1(field), QString::fromLatin1(field));
    }
    return keys;
}

// The real client; pxsteamdl::Client is thread-safe, so the batch worker may use it directly.
class PxSteamDlClient final : public WorkshopClient {
public:
    explicit PxSteamDlClient(const pxsteamdl::ClientOptions& options) : client_(options) {}

    std::vector<pxsteamdl::Result> download(std::span<const std::uint64_t> ids, const std::filesystem::path& root,
                                            const pxsteamdl::Options& options) override {
        return client_.download(ids, root, options);
    }

private:
    pxsteamdl::Client client_;
};

}  // namespace

QCoro::Task<Result<std::shared_ptr<WorkshopClient>>> login_with_pxsteamdl(LoginOptions options) {
    co_return co_await QtConcurrent::run(
        [options = std::move(options)]() -> Result<std::shared_ptr<WorkshopClient>> {
            pxsteamdl::ClientOptions native;
            native.proxy = options.proxy;
            native.connect_timeout = std::chrono::seconds(options.connect_timeout);
            native.stall_timeout = std::chrono::seconds(options.stall_timeout);
            try {
                return std::shared_ptr<WorkshopClient>(std::make_shared<PxSteamDlClient>(native));
            } catch (const std::exception& e) {
                return std::unexpected(Error{QString::fromUtf8(e.what())});
            }
        });
}

SteamBatch::SteamBatch(int total_items)
    : total(total_items), started(std::chrono::steady_clock::now()), last_logged(started) {}

QStringList SteamBatch::changed() const {
    QStringList result;
    for (const auto& id : succeeded) {
        if (bytes.value(id, {0, 0}).second > 0) {
            result.append(id);
        }
    }
    return result;
}

DownloadProgress SteamBatch::progress() const {
    // Items that report all their bytes (or 0/0 for an up-to-date copy) count as done
    // before their Result arrives.
    QSet<QString> finished(succeeded.begin(), succeeded.end());
    finished.unite(QSet<QString>(failed.begin(), failed.end()));
    qint64 bytes_done = 0;
    qint64 bytes_total = 0;
    for (auto it = bytes.cbegin(); it != bytes.cend(); ++it) {
        const auto [done, size] = it.value();
        if (done >= size) {
            finished.insert(it.key());
        }
        bytes_done += done;
        bytes_total += size;
    }
    return DownloadProgress{
        .total = total,
        .completed = static_cast<int>(finished.size()),
        .bytes_done = bytes_done,
        .bytes_total = bytes_total,
    };
}

SteamDownloader::SteamDownloader(SteamDownloaderDeps deps)
    : Downloader("steam_downloader", "Steam Workshop", deps.manager),
      config_(deps.config),
      local_mods_path_(std::move(deps.local_mods_path)),
      installed_mods_(std::move(deps.installed_mods)),
      client_factory_(std::move(deps.client_factory)),
      settings_(&deps.config, QStringLiteral("steam_downloader"), legacy_settings_keys()) {
    auto_timer_.setSingleShot(true);
    QObject::connect(&auto_timer_, &QTimer::timeout, &auto_timer_, [this] { run_scheduled_auto_update(); });
}

QCoro::Task<void> SteamDownloader::load_sync_state() {
    ConfigService& config = config_;
    auto loaded = co_await QtConcurrent::run([&config] { return config.load<WorkshopSyncState>(kWorkshopSyncFile); });
    if (loaded) {
        sync_ = std::move(*loaded);
    } else {
        spdlog::warn("[workshop] cannot read sync times: {}", loaded.error().message.toStdString());
    }
}

QCoro::Task<void> SteamDownloader::init(CoreContext&) {
    co_await load_sync_state();
    arm_auto_update(kAutoUpdateStartupDelay);
}

QCoro::Task<void> SteamDownloader::shutdown() {
    stopping_ = true;
    auto_timer_.stop();
    cancel();
    if (running_.isValid()) {
        QFuture<Outcome> pending = running_;
        co_await pending;
    }
}

bool SteamDownloader::is_downloading() const { return stop_.has_value(); }

QSet<QString> SteamDownloader::active_ids() const { return active_ids_; }

bool SteamDownloader::accepts(const QString& mod_id) const { return isAsciiDigits(mod_id); }

std::optional<QString> SteamDownloader::updatable_id(const ListedMod& mod) const {
    const QString local = local_path();
    if (local.isEmpty()) {
        return std::nullopt;
    }
    const auto& path = mod.mod_path();
    if (!path) {
        return std::nullopt;
    }
    const QFileInfo info(*path);
    if (QDir::cleanPath(info.path()) != QDir::cleanPath(local)) {
        return std::nullopt;
    }
    const auto pfid = mod.published_file_id();
    if (!pfid || *pfid != info.fileName()) {
        return std::nullopt;
    }
    return pfid;
}

std::optional<double> SteamDownloader::last_synced(const ListedMod& mod) const {
    const auto id = updatable_id(mod);
    return id ? synced_at(*id) : std::nullopt;
}

QStringList SteamDownloader::stale_ids(double max_age_s, double now) const {
    const double cutoff = now - max_age_s;
    const auto synced_or_zero = [this](const QString& id) { return synced_at(id).value_or(0.0); };
    QStringList stale;
    for (const auto& id : updatable_ids(installed_mods_())) {
        if (synced_or_zero(id) < cutoff) {
            stale.append(id);
        }
    }
    std::stable_sort(stale.begin(), stale.end(), [&](const QString& a, const QString& b) {
        return synced_or_zero(a) < synced_or_zero(b);
    });
    return stale;
}

QCoro::Task<Result<DownloadResult>> SteamDownloader::download_mods(QStringList mod_ids) {
    if (mod_ids.isEmpty()) {
        co_return std::unexpected(Error{QStringLiteral("No mods selected for download.")});
    }
    for (const auto& id : mod_ids) {
        if (!parse_item_id(id)) {
            co_return std::unexpected(
                Error{QStringLiteral("Published file IDs must be ASCII decimal numbers within uint64.")});
        }
    }
    const QString root = local_path();
    if (root.isEmpty()) {
        co_return std::unexpected(Error{QStringLiteral("Local mods path is not configured.")});
    }
    if (is_downloading()) {
        co_return std::unexpected(Error{QStringLiteral("A download is already running.")});
    }

    QStringList ids;
    QSet<QString> seen;
    for (const auto& id : mod_ids) {
        if (!seen.contains(id)) {
            seen.insert(id);
            ids.append(id);
        }
    }

    auto batch = std::make_shared<SteamBatch>(static_cast<int>(ids.size()));
    stop_.emplace();
    active_ids_ = QSet<QString>(ids.begin(), ids.end());
    busy_changed.notify(true);
    batch_started.notify(ids);
    spdlog::info("[workshop] downloading {} items into {} (first ids: {})", ids.size(), root.toStdString(),
                 ids.mid(0, 5).join(QStringLiteral(", ")).toStdString());

    try {
        co_await run_batch(ids, root, batch);
    } catch (...) {
        finish_batch(batch);
        throw;
    }
    co_return finish_batch(batch);
}

void SteamDownloader::cancel() {
    if (stop_) {
        spdlog::debug("[workshop] cancel requested");
        stop_->request_stop();
    }
}

QString SteamDownloader::local_path() const { return local_mods_path_(); }

std::optional<double> SteamDownloader::synced_at(const QString& mod_id) const {
    const auto it = sync_.synced.find(mod_id.toStdString());
    if (it == sync_.synced.end()) {
        return std::nullopt;
    }
    return it->second;
}

LoginOptions SteamDownloader::wanted_options() const {
    const auto& cfg = settings_.value();
    LoginOptions options;
    const QString proxy = QString::fromStdString(cfg.proxy).trimmed();
    if (!proxy.isEmpty()) {
        options.proxy = proxy.toStdString();
    }
    options.connect_timeout = std::max(1, cfg.connect_timeout);
    options.stall_timeout = std::max(1, cfg.stall_timeout);
    return options;
}

QCoro::Task<Result<std::shared_ptr<WorkshopClient>>> SteamDownloader::ensure_client() {
    const LoginOptions options = wanted_options();
    if (client_ && options != client_options_) {
        client_.reset();
    }
    if (!client_) {
        status_message_changed.notify(QStringLiteral(u"Logging in to Steam\u2026"));
        auto logged_in = co_await client_factory_(options);
        if (!logged_in) {
            co_return std::unexpected(logged_in.error());
        }
        client_ = std::move(*logged_in);
        client_options_ = options;
    }
    auto result = client_;
    co_return result;
}

QCoro::Task<void> SteamDownloader::run_batch(QStringList ids, QString root, std::shared_ptr<SteamBatch> batch) {
    const std::filesystem::path root_path(root.toStdWString());
    const std::error_code mkdir_error = co_await QtConcurrent::run([root_path] {
        std::error_code ec;
        std::filesystem::create_directories(root_path, ec);
        return ec;
    });
    if (mkdir_error) {
        const QString message =
            QStringLiteral("Cannot create %1: %2").arg(root, QString::fromStdString(mkdir_error.message()));
        spdlog::error("[workshop] {}", message.toStdString());
        status_message_changed.notify(message);
        for (const auto& id : ids) {
            finish_item(*batch, id, message);
        }
        co_return;
    }

    if (!client_) {
        download_phase_changed.notify(QStringLiteral("login"));
    }
    const auto login_started = std::chrono::steady_clock::now();
    auto login = co_await ensure_client();
    if (!login) {
        const QString message = QStringLiteral("Steam login failed: %1").arg(login.error().message);
        spdlog::error("[workshop] {}", message.toStdString());
        status_message_changed.notify(message);
        for (const auto& id : ids) {
            finish_item(*batch, id, message);
        }
        co_return;
    }
    spdlog::debug("[workshop] steam session ready in {:.1f}s",
                  std::chrono::duration<double>(std::chrono::steady_clock::now() - login_started).count());
    const std::shared_ptr<WorkshopClient> client = *login;

    const auto& cfg = settings_.value();
    const int parallel_items = std::clamp(cfg.parallel_items, 1, kSteamMaxParallelItems);
    const int threads_per_item = std::clamp(cfg.threads_per_item, 1, kSteamMaxThreadsPerItem);
    spdlog::debug("[workshop] client.download start: {} items, {} parallel x {} threads", ids.size(), parallel_items,
                  threads_per_item);

    std::vector<std::uint64_t> item_ids;
    item_ids.reserve(ids.size());
    for (const auto& id : ids) {
        item_ids.push_back(*parse_item_id(id));  // validated by download_mods
    }

    // PxSteamDL calls these on its worker threads. Their state lives on the main thread,
    // so each call is posted there.
    QObject* const target = &dispatch_;
    pxsteamdl::Options options;
    options.parallel_items = static_cast<unsigned>(parallel_items);
    options.threads_per_item = static_cast<unsigned>(threads_per_item);
    options.stop = stop_->get_token();
    options.on_resolved = [this, target, batch](const pxsteamdl::ItemInfo& info) {
        QMetaObject::invokeMethod(
            target,
            [this, batch, id = item_id_text(info.item_id), title = QString::fromStdString(info.title)] {
                on_resolved(*batch, id, title);
            },
            Qt::QueuedConnection);
    };
    options.on_progress = [this, target, batch](const pxsteamdl::Progress& p) {
        QMetaObject::invokeMethod(
            target,
            [this, batch, id = item_id_text(p.item_id), done = static_cast<qint64>(p.unpacked_bytes),
             total = static_cast<qint64>(p.unpacked_total)] { on_progress(*batch, id, done, total); },
            Qt::QueuedConnection);
    };

    download_phase_changed.notify(QStringLiteral("query"));
    QFuture<Outcome> call = QtConcurrent::run(
        [client, item_ids = std::move(item_ids), root_path, options]() -> Outcome {
            try {
                return client->download(item_ids, root_path, options);
            } catch (const std::runtime_error& e) {
                // Anything else propagates to the caller, as in the Python version.
                const std::string what = e.what();
                return std::unexpected(what.empty() ? std::string("download failed") : what);
            }
        });
    running_ = call;
    Outcome outcome;
    try {
        outcome = co_await QFuture<Outcome>(call);
    } catch (...) {
        running_ = {};
        throw;
    }
    running_ = {};

    if (!outcome) {
        // A failed call leaves the Steam session suspect; log in again next time.
        client_.reset();
        const QString message = QString::fromStdString(outcome.error());
        for (const auto& id : ids) {
            finish_item(*batch, id, message);
        }
        co_return;
    }

    const auto& results = *outcome;
    // A network failure leaves the Steam session suspect; log in again next time.
    if (std::ranges::any_of(results,
                            [](const pxsteamdl::Result& r) { return r.error_kind == pxsteamdl::ErrorKind::kNetwork; })) {
        client_.reset();
    }

    // Progress posted by the worker is queued before the completion that resumes this
    // coroutine, so the item callbacks have already been handled here.
    QHash<QString, const pxsteamdl::Result*> by_id;
    for (const auto& r : results) {
        by_id.insert(item_id_text(r.item_id), &r);
    }
    for (const auto& id : ids) {
        const pxsteamdl::Result* result = by_id.value(id, nullptr);
        if (!result) {
            finish_item(*batch, id, QStringLiteral("no result"));
        } else if (!result->cancelled()) {
            finish_item(*batch, id, QString::fromStdString(result->error));
        }
    }
}

DownloadResult SteamDownloader::finish_batch(const std::shared_ptr<SteamBatch>& batch) {
    stop_.reset();
    active_ids_.clear();
    busy_changed.notify(false);

    DownloadResult result{
        .succeeded = batch->succeeded,
        .failed = batch->failed,
        .changed = batch->changed(),
    };
    spdlog::info("[workshop] finished in {:.1f}s: {} ok, {} failed{}",
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - batch->started).count(),
                 result.succeeded.size(), result.failed.size(),
                 result.failed.isEmpty()
                     ? std::string()
                     : " (failed ids: " + result.failed.mid(0, 20).join(QStringLiteral(", ")).toStdString() + ")");
    record_synced(result.succeeded);
    download_finished.notify(result);
    return result;
}

void SteamDownloader::finish_item(SteamBatch& batch, const QString& mod_id, const QString& error) {
    const qint64 total = batch.bytes.value(mod_id, {0, 0}).second;
    DownloadItemStatus status;
    if (!error.isEmpty()) {
        spdlog::warn("[workshop] {} failed: {}", mod_id.toStdString(), error.toStdString());
        batch.failed.append(mod_id);
        status = DownloadItemStatus{
            .mod_id = mod_id,
            .status = QStringLiteral("error"),
            .bytes_done = 0,
            .bytes_total = 0,
            .error = error,
        };
    } else {
        batch.succeeded.append(mod_id);
        batch.bytes.insert(mod_id, {total, total});
        status = DownloadItemStatus{
            .mod_id = mod_id,
            .status = QStringLiteral("success"),
            .bytes_done = total,
            .bytes_total = total,
            .error = {},
        };
    }
    download_item_status_changed.notify(status);
    download_progress.notify(batch.progress());
}

void SteamDownloader::record_synced(const QStringList& ids) {
    if (ids.isEmpty()) {
        return;
    }
    const double now = unix_now();
    for (const auto& id : ids) {
        sync_.synced[id.toStdString()] = now;
    }
    if (auto saved = config_.save(kWorkshopSyncFile, sync_); !saved) {
        spdlog::warn("[workshop] cannot save sync times: {}", saved.error().message.toStdString());
    }
}

void SteamDownloader::on_resolved(SteamBatch& batch, const QString& mod_id, const QString& title) {
    ++batch.resolved;
    if (!title.isEmpty()) {
        download_item_titled.notify(DownloadItemTitle{.mod_id = mod_id, .title = title});
    }
    if (batch.resolved >= batch.total) {
        download_phase_changed.notify(QStringLiteral("run"));
    }
}

void SteamDownloader::on_progress(SteamBatch& batch, const QString& mod_id, qint64 done, qint64 total) {
    if (batch.succeeded.contains(mod_id) || batch.failed.contains(mod_id)) {
        return;
    }
    if (!batch.bytes.contains(mod_id)) {
        ++batch.seen;
        spdlog::debug("[workshop] {} first progress: {}/{} bytes (item {} of {})", mod_id.toStdString(), done,
                      total, batch.seen, batch.total);
    }
    batch.bytes.insert(mod_id, {done, total});

    // PxSteamDL reports per network chunk, thousands of times a second; the UI needs far fewer.
    const auto now = std::chrono::steady_clock::now();
    if (done >= total || now - batch.last_emitted >= kProgressEmitInterval) {
        batch.last_emitted = now;
        emit_item_progress(batch, mod_id, done, total);
    }
    if (now - batch.last_logged >= kProgressLogInterval) {
        batch.last_logged = now;
        const DownloadProgress progress = batch.progress();
        spdlog::info("[workshop] progress: {}/{} items, {:.1f} MB, {:.0f}s elapsed", progress.completed,
                     progress.total, static_cast<double>(progress.bytes_done) / 1048576.0,
                     std::chrono::duration<double>(now - batch.started).count());
    }
}

void SteamDownloader::emit_item_progress(SteamBatch& batch, const QString& mod_id, qint64 done, qint64 total) {
    download_item_status_changed.notify(DownloadItemStatus{
        .mod_id = mod_id,
        .status = QStringLiteral("downloading"),
        .bytes_done = done,
        .bytes_total = total,
        .error = {},
    });
    download_progress.notify(batch.progress());
}

QCoro::Task<void> SteamDownloader::auto_update_once() {
    if (is_downloading()) {
        co_return;
    }
    const int hours = settings_.value().auto_update_hours;
    if (hours <= 0) {
        co_return;
    }
    const QStringList ids = stale_ids(hours * 3600.0, unix_now());
    if (ids.isEmpty()) {
        co_return;
    }
    spdlog::info("[workshop] auto-update: {} mods older than {}h", ids.size(), hours);
    const auto result = co_await download_mods(ids);
    if (!result) {
        spdlog::warn("[workshop] auto-update not started: {}", result.error().message.toStdString());
    }
}

void SteamDownloader::arm_auto_update(std::chrono::milliseconds delay) {
    if (!stopping_) {
        auto_timer_.start(delay);
    }
}

QCoro::Task<void> SteamDownloader::run_scheduled_auto_update() {
    try {
        co_await auto_update_once();
    } catch (const std::exception& e) {
        spdlog::error("[workshop] auto-update iteration failed: {}", e.what());
    }
    arm_auto_update(kAutoUpdateCheckInterval);
}

}  // namespace px::modrim

#endif  // PX_HAS_STEAM
