#pragma once

#include <any>
#include <coroutine>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <glaze/glaze.hpp>
#include <QCoro/QCoroFuture>
#include <QCoro/QCoroTask>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>
#include <spdlog/spdlog.h>

#include "core/config.h"
#include "core/downloads/manager.h"
#include "core/error.h"
#include "core/events.h"
#include "core/file_io.h"
#include "core/models/metadata/structures.h"
#include "core/plugin.h"
#include "core/plugin_config.h"
#include "core/workshop/client.h"
#include "core/workshop/favourites.h"
#include "core/workshop/types.h"

namespace px::modrim::workshop {

inline constexpr std::string_view kDefaultCatalogUrl = "https://api.modrim.pyxiion.dev";

// A value remembered for less than this many seconds is served without contacting the catalog.
inline constexpr double kFreshSeconds = 300.0;

// Stored as plugins/workshop_catalog.json.
struct CatalogSettings {
    std::string url = std::string(kDefaultCatalogUrl);

    bool operator==(const CatalogSettings&) const = default;
};

// The installed mods the catalog compares against. The composition root supplies it, so the catalog
// does not reach for global state.
using InstalledModsProvider = std::function<QList<ListedModPtr>()>;

// A cached value with the time it was fetched (seconds since the epoch).
template <class T>
struct Remembered {
    double at = 0;
    T value{};
};

namespace detail {

// A missing, unreadable or undecodable cache file is treated as a miss.
template <class T>
std::optional<Remembered<T>> read_cached(const QString& path) {
    auto bytes = read_file_bytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    Remembered<T> entry;
    if (glz::read<kWireOpts>(entry, *bytes)) {
        return std::nullopt;
    }
    return entry;
}

// Writes one cache file, then drops the oldest files beyond the disk limit.
Result<void> write_cached(const QString& directory, const QString& path, const std::string& text);

}  // namespace detail

// The Workshop catalog: remote browsing and resolution of mods, plus a download queue that runs behind
// the downloads manager. The catalog never mutates installed mods itself.
class WorkshopCatalog : public Plugin {
public:
    WorkshopCatalog();
    void setup(CoreContext& ctx) override;
    // `config` may be null: settings, favourites and the response cache then stay in memory.
    WorkshopCatalog(ConfigService* config, DownloadManager& downloads, InstalledModsProvider installed_mods);
    ~WorkshopCatalog();

    WorkshopCatalog(const WorkshopCatalog&) = delete;
    WorkshopCatalog& operator=(const WorkshopCatalog&) = delete;

    // Stops the queue and aborts requests in flight.
    QCoro::Task<void> shutdown() override;

    Event<QString> catalog_url_changed;
    Event<std::monostate> installed_changed;
    Event<std::monostate> queue_changed;

    const CatalogSettings& settings() const noexcept { return settings_.value(); }
    void set_base_url(const QString& url);
    bool configured() const noexcept { return !base_url_.isEmpty(); }

    FavouriteCollections& favourites() noexcept { return favourites_; }

    QCoro::Task<CatalogResult<Discover>> discover();
    QCoro::Task<CatalogResult<CatalogPage<CatalogMod>>> mods(CatalogQuery query);
    QCoro::Task<CatalogResult<CatalogPage<CatalogCollection>>> collections(CatalogQuery query);
    QCoro::Task<CatalogResult<std::optional<CatalogMod>>> mod(QString id);
    QCoro::Task<CatalogResult<std::optional<CollectionDetail>>> collection(QString id);

    // Serves the remembered value at once through `show`, then the refreshed one when it differs.
    // A value younger than `fresh_for` is served without contacting the catalog. With `persist`, the
    // value also survives a restart. `T` must be serialisable with glaze and comparable.
    template <class T>
    QCoro::Task<CatalogResult<void>> cached(QString key, std::function<QCoro::Task<CatalogResult<T>>()> produce,
                                            std::function<void(const T&)> show, double fresh_for = kFreshSeconds,
                                            bool persist = false);

    // Installed state is cached until invalidate_installed().
    InstallState install_state(const CatalogMod& mod);
    QCoro::Task<CatalogResult<std::vector<CatalogMod>>> installed_with_updates();

    // Resolves ids into the mods to fetch, skipping those already current.
    QCoro::Task<CatalogResult<DownloadPlan>> plan(QStringList mod_ids, QStringList collection_ids);
    QCoro::Task<Result<DownloadResult>> download(DownloadPlan download_plan);

    // Ids waiting in the queue or downloading.
    QSet<QString> queued_ids() const { return queued_; }
    // Queues `plan` behind any running download without waiting for it.
    void enqueue(const DownloadPlan& download_plan);
    void cancel_queue();

    // Call when the installed mods changed (mods changed, config changed).
    void invalidate_installed();

private:
    using InstalledIndex = QHash<QString, QList<ListedModPtr>>;

    // Suspends the queue until the downloads report they are idle again.
    struct IdleWait {
        WorkshopCatalog& catalog;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) const { catalog.idle_waiters_.push_back(handle); }
        void await_resume() const noexcept {}
    };

    CatalogClient& client();
    const InstalledIndex& installed();
    void bind_downloads(DownloadManager& downloads);
    void disconnect_events();
    InstallState install_state_of(const CatalogMod& mod, const InstalledIndex& installed) const;
    void change_url(const QString& url);
    void on_busy_changed(bool busy);
    QCoro::Task<void> drain();

    template <class T>
    std::optional<Remembered<T>> recall(const QString& key) const;
    // Keeps the memory cache within its limit. `move_to_end` marks the entry as the newest.
    void remember(const QString& key, std::any value, bool move_to_end);
    QString cache_file(const QString& key) const;

    PluginConfig<CatalogSettings> settings_;
    QString base_url_;
    std::optional<QString> cache_dir_;
    FavouriteCollections favourites_;
    DownloadManager* downloads_ = nullptr;
    InstalledModsProvider installed_mods_;
    Event<DownloadResult>::Id download_finished_id_ = 0;
    Event<bool>::Id busy_changed_id_ = 0;
    CoreContext* context_ = nullptr;
    Event<std::monostate>::Id mods_changed_id_ = 0;
    Event<AppConfig>::Id config_changed_id_ = 0;

    std::optional<InstalledIndex> installed_index_;
    // Insertion-ordered: the oldest entries are evicted first.
    QList<QString> response_order_;
    // Each value is a Remembered<T> for the T it was stored with.
    QHash<QString, std::any> responses_;

    // Every client made so far stays alive until shutdown, so requests in flight on a replaced client finish.
    std::vector<std::unique_ptr<CatalogClient>> clients_;
    CatalogClient* client_ = nullptr;

    QList<QStringList> queue_;
    QSet<QString> queued_;
    bool draining_ = false;
    std::optional<QCoro::Task<void>> runner_;
    std::vector<std::coroutine_handle<>> idle_waiters_;
};

template <class T>
std::optional<Remembered<T>> WorkshopCatalog::recall(const QString& key) const {
    const auto found = responses_.constFind(key);
    if (found == responses_.constEnd()) {
        return std::nullopt;
    }
    const auto* typed = std::any_cast<Remembered<T>>(&found.value());
    if (!typed) {
        return std::nullopt;
    }
    return *typed;
}

template <class T>
QCoro::Task<CatalogResult<void>> WorkshopCatalog::cached(QString key,
                                                         std::function<QCoro::Task<CatalogResult<T>>()> produce,
                                                         std::function<void(const T&)> show, double fresh_for,
                                                         bool persist) {
    const QString full_key = base_url_ + QLatin1Char('|') + key;
    std::optional<QString> path;
    if (persist && cache_dir_) {
        path = cache_file(full_key);
    }

    std::optional<Remembered<T>> entry = recall<T>(full_key);
    if (!entry && path) {
        entry = co_await QtConcurrent::run([path = *path] { return detail::read_cached<T>(path); });
        if (entry) {
            remember(full_key, *entry, false);
        }
    }
    if (entry) {
        show(entry->value);
        const double age = QDateTime::currentMSecsSinceEpoch() / 1000.0 - entry->at;
        if (age < fresh_for) {
            co_return {};
        }
    }

    auto fetched = co_await produce();
    if (!fetched) {
        co_return std::unexpected(fetched.error());
    }
    T value = std::move(*fetched);
    const bool changed = !entry || value != entry->value;
    if (entry && !changed) {
        // Keep the displayed copy so an equal refresh does not replace what is on screen.
        value = entry->value;
    }
    const Remembered<T> remembered{QDateTime::currentMSecsSinceEpoch() / 1000.0, value};
    remember(full_key, remembered, true);

    if (path) {
        auto encoded = glz::write<kWireOpts>(remembered);
        if (encoded) {
            auto saved = co_await QtConcurrent::run(
                [directory = *cache_dir_, file = *path, bytes = std::move(*encoded)] {
                    return detail::write_cached(directory, file, bytes);
                });
            if (!saved) {
                spdlog::warn("[workshop] cannot write cache: {}", saved.error().message.toStdString());
            }
        }
    }
    if (changed) {
        show(value);
    }
    co_return {};
}

}  // namespace px::modrim::workshop
