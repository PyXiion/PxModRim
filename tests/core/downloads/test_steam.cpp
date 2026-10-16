#ifdef PX_HAS_STEAM

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pxsteamdl/pxsteamdl.hpp>
#include <QCoro/QCoroTask>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include "../test_support.hpp"
#include "core/config.hpp"
#include "core/downloads/manager.hpp"
#include "core/downloads/steam.hpp"
#include "core/downloads/types.hpp"
#include "core/models/metadata/structures.hpp"

namespace {

using namespace std::chrono_literals;

using px::modrim::ConfigService;
using px::modrim::DownloadItemStatus;
using px::modrim::DownloadManager;
using px::modrim::DownloadProgress;
using px::modrim::DownloadResult;
using px::modrim::ListedMod;
using px::modrim::ListedModPtr;
using px::modrim::LoginOptions;
using px::modrim::Result;
using px::modrim::SteamBatch;
using px::modrim::SteamDownloader;
using px::modrim::SteamDownloaderDeps;
using px::modrim::SteamSettings;
using px::modrim::WorkshopClient;
using px::modrim::WorkshopSyncState;
using test_support::TestApplication;

// Thread-safe flag with a timeout-aware wait.
class Flag {
public:
    void set() {
        {
            std::lock_guard lock(m_mutex);
            m_value = true;
        }
        m_cv.notify_all();
    }

    void clear() {
        std::lock_guard lock(m_mutex);
        m_value = false;
    }

    bool isSet() const {
        std::lock_guard lock(m_mutex);
        return m_value;
    }

    bool waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [this] { return m_value; });
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_value = false;
};

// Drives an event loop until `task` co_returns. Fails if `timeout` expires.
template <class T>
QCoro::Task<void> drive(QCoro::Task<T> task, std::optional<T>& out, QEventLoop& loop) {
    out.emplace(co_await std::move(task));
    loop.quit();
}

template <class T>
T runLoop(QCoro::Task<T> task, std::chrono::milliseconds timeout = 10s) {
    std::optional<T> out;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&loop] { loop.quit(); });
    timer.start(timeout);

    auto driver = drive(std::move(task), out, loop);
    if (!out.has_value()) {
        loop.exec();
    }
    REQUIRE(out.has_value());
    return std::move(*out);
}

inline QCoro::Task<void> driveVoid(QCoro::Task<void> task, bool& done, QEventLoop& loop) {
    co_await std::move(task);
    done = true;
    loop.quit();
}

inline void runLoopVoid(QCoro::Task<void> task, std::chrono::milliseconds timeout = 10s) {
    bool done = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&loop] { loop.quit(); });
    timer.start(timeout);

    auto driver = driveVoid(std::move(task), done, loop);
    if (!done) {
        loop.exec();
    }
    REQUIRE(done);
}

class FakeClient : public WorkshopClient {
public:
    struct Call {
        std::vector<std::uint64_t> ids;
        QString root;
        int workers = 0;
    };

    QHash<std::uint64_t, QString> errors;
    QHash<std::uint64_t, std::shared_ptr<Flag>> gates;
    QSet<std::uint64_t> networkFailures;
    std::shared_ptr<Flag> blocked = std::make_shared<Flag>();

    std::vector<Call> calls;

    std::vector<pxsteamdl::Result> download(std::span<const std::uint64_t> ids, const std::filesystem::path& root,
                                            const pxsteamdl::Options& options) override {
        calls.push_back(Call{
            .ids = std::vector<std::uint64_t>(ids.begin(), ids.end()),
            .root = QString::fromStdWString(root.wstring()),
            .workers = static_cast<int>(options.parallel_items * options.threads_per_item),
        });

        std::vector<pxsteamdl::Result> results;
        results.reserve(ids.size());
        for (const auto id : ids) {
            if (options.on_resolved) {
                pxsteamdl::ItemInfo info;
                info.item_id = id;
                info.title = "Title " + std::to_string(id);
                options.on_resolved(info);
            }
            if (options.on_progress) {
                pxsteamdl::Progress p;
                p.item_id = id;
                p.unpacked_bytes = 5;
                p.unpacked_total = 10;
                options.on_progress(p);
            }
            if (auto gate = gates.value(id)) {
                blocked->set();
                gate->waitFor(5s);
            }

            const bool cancelled = options.stop.stop_requested();
            const QString errorText = cancelled ? QStringLiteral("cancelled") : errors.value(id);
            if (errorText.isEmpty()) {
                std::error_code ec;
                std::filesystem::create_directories(root / std::to_string(id), ec);
            }

            pxsteamdl::ErrorKind kind = pxsteamdl::ErrorKind::kNone;
            if (networkFailures.contains(id)) {
                kind = pxsteamdl::ErrorKind::kNetwork;
            } else if (cancelled) {
                kind = pxsteamdl::ErrorKind::kCancelled;
            } else if (!errorText.isEmpty()) {
                kind = pxsteamdl::ErrorKind::kOther;
            }

            results.push_back(pxsteamdl::Result{
                .item_id = id,
                .title = "Title " + std::to_string(id),
                .path = root / std::to_string(id),
                .error = errorText.toStdString(),
                .error_kind = kind,
                .downloaded_bytes = 0,
                .unpacked_bytes = 0,
            });
        }
        return results;
    }
};

class DyingClient : public WorkshopClient {
public:
    std::vector<pxsteamdl::Result> download(std::span<const std::uint64_t>, const std::filesystem::path&,
                                            const pxsteamdl::Options&) override {
        throw std::runtime_error("session lost");
    }
};

struct Service {
    explicit Service(std::deque<std::shared_ptr<WorkshopClient>> clients, bool local = true)
        : configDir(),
          config(configDir.path()),
          manager(),
          localPath(local ? configDir.path() + "/Mods" : QString()),
          clients(std::move(clients)) {
        SteamDownloaderDeps deps{
            .manager = manager,
            .config = config,
            .localModsPath = [this] { return localPath; },
            .installedMods = [this] { return mods; },
            .clientFactory = [this](LoginOptions opts) { return login(std::move(opts)); },
        };
        steam = std::make_unique<SteamDownloader>(std::move(deps));
        manager.registerDownloader(*steam);
    }

    QCoro::Task<Result<std::shared_ptr<WorkshopClient>>> login(LoginOptions opts) {
        logins.push_back(opts);
        if (clients.empty()) {
            co_return std::unexpected(px::modrim::Error{QStringLiteral("logon denied")});
        }
        auto client = clients.front();
        clients.pop_front();
        co_return client;
    }

    QTemporaryDir configDir;
    ConfigService config;
    DownloadManager manager;
    QString localPath;
    QList<ListedModPtr> mods;

    std::vector<LoginOptions> logins;
    std::deque<std::shared_ptr<WorkshopClient>> clients;
    std::unique_ptr<SteamDownloader> steam;
};

ListedModPtr makeModAt(const QString& path, const QString& pfid = {}) {
    QDir().mkpath(path);
    if (!pfid.isEmpty()) {
        QDir().mkpath(path + "/About");
        test_support::writeBytes(path + "/About/PublishedFileId.txt", pfid.toStdString());
    }
    auto mod = std::make_shared<ListedMod>();
    REQUIRE(mod->setModPath(path).has_value());
    return mod;
}

std::optional<double> syncedOf(Service& svc, const QString& pid) {
    auto mod = std::make_shared<ListedMod>();
    REQUIRE(mod->setModPath(svc.localPath + "/" + pid).has_value());
    return svc.steam->lastSynced(*mod);
}

}  // namespace

TEST_CASE("login uses configured proxy and timeouts", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client, client});

    REQUIRE(runLoop(svc.steam->downloadMods({"111"})).has_value());
    svc.steam->settings().update(SteamSettings{
        .auto_update_hours = 0,
        .parallel_items = 8,
        .threads_per_item = 1,
        .proxy = " socks5h://host:1080 ",
        .connect_timeout = 3,
        .stall_timeout = 7,
    });
    REQUIRE(runLoop(svc.steam->downloadMods({"222"})).has_value());

    const std::vector<LoginOptions> expected{
        LoginOptions{.proxy = std::nullopt, .connectTimeout = 10, .stallTimeout = 30},
        LoginOptions{.proxy = "socks5h://host:1080", .connectTimeout = 3, .stallTimeout = 7},
    };
    REQUIRE(svc.logins == expected);
}

TEST_CASE("network failure triggers new login", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    client->networkFailures.insert(111);
    Service svc({client, client});

    REQUIRE(runLoop(svc.steam->downloadMods({"111"})).has_value());
    REQUIRE(runLoop(svc.steam->downloadMods({"222"})).has_value());

    REQUIRE(svc.logins.size() == 2);
}

TEST_CASE("downloads into local mods and splits results", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    client->errors.insert(222, QStringLiteral("item not found"));
    Service svc({client});

    std::vector<DownloadItemStatus> statuses;
    std::vector<DownloadResult> finished;
    svc.steam->downloadItemStatusChanged.connect([&](const auto& s) { statuses.push_back(s); });
    svc.steam->downloadFinished.connect([&](const auto& r) { finished.push_back(r); });

    auto result = runLoop(svc.steam->downloadMods({"111", "222", "111"}));

    REQUIRE(result.has_value());
    REQUIRE(result->succeeded == QStringList{"111"});
    REQUIRE(result->failed == QStringList{"222"});
    REQUIRE(finished.size() == 1);
    REQUIRE(finished[0] == *result);
    REQUIRE(QFileInfo::exists(svc.localPath + "/111"));
    REQUIRE(client->calls.size() == 1);
    REQUIRE(client->calls[0].ids == std::vector<std::uint64_t>{111, 222});
    REQUIRE(client->calls[0].root == svc.localPath);

    QHash<QString, DownloadItemStatus> finalStatus;
    for (const auto& s : statuses) {
        if (s.status != "downloading") {
            finalStatus.insert(s.modId, s);
        }
    }
    REQUIRE(finalStatus.value("111").status == "success");
    REQUIRE(finalStatus.value("222").status == "error");
    REQUIRE(finalStatus.value("222").error == "item not found");
}

TEST_CASE("rejects invalid ids", "[downloads][steam]") {
    TestApplication app;
    const std::vector<QStringList> badInputs{
        {"12a"}, {"\u0661\u0662\u0663"}, {"1 2"}, {}, {"18446744073709551616"}};
    for (const auto& ids : badInputs) {
        Service svc({std::make_shared<FakeClient>()});
        auto result = runLoop(svc.steam->downloadMods(ids));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(svc.logins.empty());
    }
}

TEST_CASE("requires local mods path", "[downloads][steam]") {
    TestApplication app;
    Service svc({std::make_shared<FakeClient>()}, /*local=*/false);

    auto result = runLoop(svc.steam->downloadMods({"111"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message.contains("Local mods path"));
    REQUIRE(svc.logins.empty());
}

TEST_CASE("accepts uint64 max id", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client});

    auto result = runLoop(svc.steam->downloadMods({"18446744073709551615"}));

    REQUIRE(result.has_value());
    REQUIRE(result->succeeded == QStringList{"18446744073709551615"});
    REQUIRE(client->calls[0].ids == std::vector<std::uint64_t>{UINT64_MAX});
}

TEST_CASE("unwritable local path fails every item", "[downloads][steam]") {
    TestApplication app;
    Service svc({std::make_shared<FakeClient>()});
    test_support::writeBytes(svc.localPath, "not a directory");

    auto result = runLoop(svc.steam->downloadMods({"111", "222"}));

    REQUIRE(result.has_value());
    QStringList failed = result->failed;
    failed.sort();
    REQUIRE(failed == QStringList{"111", "222"});
    REQUIRE(svc.logins.empty());
}

TEST_CASE("login failure fails every item and retries next time", "[downloads][steam]") {
    TestApplication app;
    Service svc({});
    std::vector<QString> messages;
    svc.steam->statusMessageChanged.connect([&](const QString& m) { messages.push_back(m); });

    auto first = runLoop(svc.steam->downloadMods({"111", "222"}));
    auto second = runLoop(svc.steam->downloadMods({"111"}));

    REQUIRE(first.has_value());
    REQUIRE(first->succeeded.isEmpty());
    QStringList failed = first->failed;
    failed.sort();
    REQUIRE(failed == QStringList{"111", "222"});

    const bool hasDenied = std::ranges::any_of(messages, [](const auto& m) { return m.contains("logon denied"); });
    REQUIRE(hasDenied);
    REQUIRE(svc.logins.size() == 2);
}

TEST_CASE("client session is reused", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client});

    REQUIRE(runLoop(svc.steam->downloadMods({"111"})).has_value());
    REQUIRE(runLoop(svc.steam->downloadMods({"222"})).has_value());
    REQUIRE(svc.logins.size() == 1);
}

TEST_CASE("uses at most eight download workers", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client});
    QStringList ids;
    for (int i = 1; i <= 20; ++i) {
        ids.append(QString::number(i));
    }

    REQUIRE(runLoop(svc.steam->downloadMods(ids)).has_value());
    REQUIRE(client->calls.size() == 1);
    REQUIRE(client->calls[0].workers <= 8);
}

TEST_CASE("cancel keeps finished items and returns", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    auto gate = std::make_shared<Flag>();
    client->gates.insert(222, gate);
    Service svc({client});

    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
        if (client->blocked->isSet()) {
            svc.steam->cancel();
            gate->set();
            poll.stop();
        }
    });
    poll.start(10ms);

    auto result = runLoop(svc.steam->downloadMods({"111", "222", "333"}));

    REQUIRE(result.has_value());
    REQUIRE(result->succeeded == QStringList{"111"});
    REQUIRE(result->failed.isEmpty());
    REQUIRE_FALSE(svc.steam->isDownloading());
}

TEST_CASE("second download while running is rejected", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    auto gate = std::make_shared<Flag>();
    client->gates.insert(111, gate);
    Service svc({client});

    std::optional<Result<DownloadResult>> firstOut;
    std::optional<Result<DownloadResult>> secondOut;
    QEventLoop loop;

    auto firstTask = test_support::awaitInto(svc.steam->downloadMods({"111"}), firstOut);

    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
        if (svc.steam->isDownloading()) {
            poll.stop();
            auto secondTask = test_support::awaitInto(svc.steam->downloadMods({"222"}), secondOut);
            gate->set();
            loop.quit();
        }
    });
    poll.start(10ms);
    loop.exec();

    REQUIRE(secondOut.has_value());
    REQUIRE_FALSE(secondOut->has_value());
    REQUIRE(secondOut->error().message.contains("already running"));
}

TEST_CASE("updatable_id only for downloads in local mods", "[downloads][steam]") {
    TestApplication app;
    Service svc({});
    const QString root = svc.localPath;
    const auto m1 = makeModAt(root + "/111", "111");
    const auto m2 = makeModAt(root + "/Renamed", "222");
    const auto m3 = makeModAt(root + "/333");
    const auto m4 = makeModAt(svc.configDir.path() + "/Workshop/444", "444");
    svc.mods = {m1, m2, m3, m4};

    REQUIRE(svc.steam->updatableId(*m1) == std::optional<QString>("111"));
    REQUIRE(svc.steam->updatableId(*m2) == std::nullopt);
    REQUIRE(svc.steam->updatableId(*m3) == std::optional<QString>("333"));
    REQUIRE(svc.steam->updatableId(*m4) == std::nullopt);

    REQUIRE(svc.steam->updatableIds(svc.mods) == QStringList{"111", "333"});
}

TEST_CASE("updatable_id none without local path", "[downloads][steam]") {
    TestApplication app;
    Service svc({}, /*local=*/false);
    const auto mod = makeModAt(svc.configDir.path() + "/Mods/111", "111");
    REQUIRE(svc.steam->updatableId(*mod) == std::nullopt);
}

TEST_CASE("busy changed brackets download", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client});
    std::vector<QString> events;
    svc.steam->busyChanged.connect([&](bool busy) { events.push_back(busy ? "busy:1" : "busy:0"); });
    svc.steam->downloadFinished.connect([&](const auto&) { events.push_back("finished"); });

    REQUIRE(runLoop(svc.steam->downloadMods({"111"})).has_value());

    const std::vector<QString> expected{"busy:1", "busy:0", "finished"};
    REQUIRE(events == expected);
}

TEST_CASE("invalid ids do not toggle busy", "[downloads][steam]") {
    TestApplication app;
    Service svc({std::make_shared<FakeClient>()});
    std::vector<bool> busy;
    svc.steam->busyChanged.connect([&](bool b) { busy.push_back(b); });

    auto result = runLoop(svc.steam->downloadMods({"12a"}));
    REQUIRE_FALSE(result.has_value());
    REQUIRE(busy.empty());
}

TEST_CASE("progress counts complete and up to date items", "[downloads][steam]") {
    SteamBatch batch(3);
    batch.bytes.insert("1", {10, 10});
    batch.bytes.insert("2", {5, 10});
    batch.bytes.insert("3", {0, 0});
    REQUIRE(batch.progress().completed == 2);

    batch.succeeded.append("1");
    REQUIRE(batch.progress().completed == 2);
}

TEST_CASE("updatable_ids limited to given mods and deduplicated", "[downloads][steam]") {
    TestApplication app;
    Service svc({});
    const auto a = makeModAt(svc.localPath + "/111", "111");
    const auto b = makeModAt(svc.localPath + "/222", "222");
    const auto other = makeModAt(svc.configDir.path() + "/Other/9", "9");

    REQUIRE(svc.steam->updatableIds({b, b, other}) == QStringList{"222"});
}

TEST_CASE("result changed excludes up to date items", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client});

    auto result = runLoop(svc.steam->downloadMods({"111"}));

    REQUIRE(result.has_value());
    REQUIRE(result->changed == QStringList{"111"});
    REQUIRE(svc.steam->activeIds().isEmpty());
}

TEST_CASE("batch changed ignores zero byte items", "[downloads][steam]") {
    SteamBatch batch(2);
    batch.succeeded = {"1", "2"};
    batch.bytes.insert("1", {0, 0});
    batch.bytes.insert("2", {5, 5});
    REQUIRE(batch.changed() == QStringList{"2"});
}

TEST_CASE("success records sync time and persists", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    client->errors.insert(222, QStringLiteral("gone"));
    Service svc({client});

    REQUIRE(runLoop(svc.steam->downloadMods({"111", "222"})).has_value());
    REQUIRE(syncedOf(svc, "111").has_value());
    REQUIRE_FALSE(syncedOf(svc, "222").has_value());

    auto reloaded = svc.config.load<WorkshopSyncState>(px::modrim::kWorkshopSyncFile);
    REQUIRE(reloaded.has_value());
    std::vector<std::string> keys;
    for (const auto& [k, _] : reloaded->synced) {
        keys.push_back(k);
    }
    REQUIRE(keys == std::vector<std::string>{"111"});
}

TEST_CASE("stale ids never synced first then oldest", "[downloads][steam]") {
    TestApplication app;
    Service svc({});
    for (const char* name : {"1", "2", "3", "4"}) {
        svc.mods.push_back(makeModAt(svc.localPath + "/" + name, name));
    }
    WorkshopSyncState seed;
    seed.synced = {{"1", 1000.0}, {"2", 9000.0}, {"3", 500.0}};
    REQUIRE(svc.config.save(px::modrim::kWorkshopSyncFile, seed).has_value());
    runLoopVoid(svc.steam->loadSyncState());

    REQUIRE(svc.steam->staleIds(3600.0, 10000.0) == QStringList{"4", "3", "1"});
}

TEST_CASE("auto update only syncs stale mods when enabled", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client});
    svc.mods.push_back(makeModAt(svc.localPath + "/1", "1"));

    runLoopVoid(svc.steam->autoUpdateOnce());
    REQUIRE_FALSE(syncedOf(svc, "1").has_value());

    svc.steam->settings().update(SteamSettings{
        .auto_update_hours = 6,
        .parallel_items = 8,
        .threads_per_item = 1,
        .proxy = {},
        .connect_timeout = 10,
        .stall_timeout = 30,
    });
    runLoopVoid(svc.steam->autoUpdateOnce());
    REQUIRE(syncedOf(svc, "1").has_value());
    const double firstSynced = *syncedOf(svc, "1");

    runLoopVoid(svc.steam->autoUpdateOnce());
    REQUIRE(*syncedOf(svc, "1") == firstSynced);
}

TEST_CASE("titles from steam are emitted", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client});
    QHash<QString, QString> titles;
    svc.steam->downloadItemTitled.connect([&](const auto& t) { titles.insert(t.modId, t.title); });
    std::vector<QString> phases;
    svc.steam->downloadPhaseChanged.connect([&](const auto& p) { phases.push_back(p); });

    REQUIRE(runLoop(svc.steam->downloadMods({"111", "222"})).has_value());

    REQUIRE(phases == std::vector<QString>{"login", "query", "run"});
    REQUIRE(titles.value("111") == "Title 111");
    REQUIRE(titles.value("222") == "Title 222");
}

TEST_CASE("client is relogged after download raises", "[downloads][steam]") {
    TestApplication app;
    auto dying = std::make_shared<DyingClient>();
    auto fresh = std::make_shared<FakeClient>();
    Service svc({dying, fresh});

    auto first = runLoop(svc.steam->downloadMods({"1"}));
    REQUIRE(first.has_value());
    REQUIRE(first->failed == QStringList{"1"});

    auto second = runLoop(svc.steam->downloadMods({"2"}));
    REQUIRE(second.has_value());
    REQUIRE(second->succeeded == QStringList{"2"});
    REQUIRE(svc.logins.size() == 2);
}

TEST_CASE("download uses configured parallelism clamped", "[downloads][steam]") {
    TestApplication app;
    auto client = std::make_shared<FakeClient>();
    Service svc({client});

    svc.steam->settings().update(SteamSettings{
        .auto_update_hours = 0,
        .parallel_items = 3,
        .threads_per_item = 5,
        .proxy = {},
        .connect_timeout = 10,
        .stall_timeout = 30,
    });
    REQUIRE(runLoop(svc.steam->downloadMods({"1"})).has_value());

    svc.steam->settings().update(SteamSettings{
        .auto_update_hours = 0,
        .parallel_items = 0,
        .threads_per_item = 999,
        .proxy = {},
        .connect_timeout = 10,
        .stall_timeout = 30,
    });
    REQUIRE(runLoop(svc.steam->downloadMods({"2"})).has_value());

    REQUIRE(client->calls.size() == 2);
    REQUIRE(client->calls[0].workers == 15);
    REQUIRE(client->calls[1].workers == 16);
}

#endif  // PX_HAS_STEAM
