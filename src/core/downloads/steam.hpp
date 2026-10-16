#pragma once

// Everything here talks to PxSteamDL, so the whole file exists only when Steam support is built.
#ifdef PX_HAS_STEAM

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include <pxsteamdl/pxsteamdl.hpp>
#include <QCoro/QCoroTask>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "core/config.hpp"
#include "core/downloads/downloader.hpp"
#include "core/downloads/types.hpp"
#include "core/error.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/plugin_config.hpp"

namespace px::modrim {

// PxSteamDL runs one shared pool of parallel_items * threads_per_item workers.
inline constexpr int kSteamMaxParallelItems = 8;
inline constexpr int kSteamMaxThreadsPerItem = 16;

inline constexpr std::chrono::seconds kAutoUpdateStartupDelay{45};
inline constexpr std::chrono::seconds kAutoUpdateCheckInterval{600};

// In the config folder; records when each Workshop item was last synced.
inline const QString kWorkshopSyncFile = QStringLiteral("workshop_sync.json");

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or on the wire
struct SteamSettings {
    int auto_update_hours = 0;
    int parallel_items = 8;
    int threads_per_item = 1;
    std::string proxy;
    int connect_timeout = 10;
    int stall_timeout = 30;

    bool operator==(const SteamSettings&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// Unix seconds of the last sync, keyed by Workshop id.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or on the wire
struct WorkshopSyncState {
    std::map<std::string, double> synced;
};
// NOLINTEND(readability-identifier-naming)

// Network settings of one Steam login. An unset proxy means libcurl's environment.
struct LoginOptions {
    std::optional<std::string> proxy;
    int connectTimeout = 10;
    int stallTimeout = 30;

    bool operator==(const LoginOptions&) const = default;
};

// The part of pxsteamdl::Client that the downloader uses. A seam: tests substitute a fake.
class WorkshopClient {
public:
    virtual ~WorkshopClient() = default;

    virtual std::vector<pxsteamdl::Result> download(std::span<const std::uint64_t> ids,
                                                    const std::filesystem::path& root,
                                                    const pxsteamdl::Options& options) = 0;
};

using ClientFactory = std::function<QCoro::Task<Result<std::shared_ptr<WorkshopClient>>>(LoginOptions)>;

// Logs in anonymously with PxSteamDL on a worker thread.
QCoro::Task<Result<std::shared_ptr<WorkshopClient>>> loginWithPxsteamdl(LoginOptions options);

// Progress of one download batch. An item counts as complete once all its bytes are in,
// which can happen before PxSteamDL returns its Result.
struct SteamBatch {
    explicit SteamBatch(int totalItems);

    int total = 0;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point lastLogged;
    std::chrono::steady_clock::time_point lastEmitted;
    int seen = 0;
    int resolved = 0;
    // Item id -> (bytes done, bytes total).
    QHash<QString, std::pair<qint64, qint64>> bytes;
    QStringList succeeded;
    QStringList failed;

    // Succeeded items that transferred data; up-to-date items are excluded.
    QStringList changed() const;
    DownloadProgress progress() const;
};

// What the downloader reads from the application. Passed in rather than taken from a
// CoreContext so the downloader can be built and tested without the whole application.
struct SteamDownloaderDeps {
    DownloadManager& manager;
    ConfigService& config;
    // The configured local mods folder; empty when not set.
    std::function<QString()> localModsPath;
    // Mods the application knows about; auto-update picks the stale ones among them.
    std::function<QList<ListedModPtr>()> installedMods;
    ClientFactory clientFactory = loginWithPxsteamdl;
};

// Downloads Steam Workshop items into the local mods folder via PxSteamDL.
class SteamDownloader : public Downloader {
public:
    explicit SteamDownloader(SteamDownloaderDeps deps);

    QCoro::Task<void> init(CoreContext& ctx) override;
    QCoro::Task<void> shutdown() override;

    bool isDownloading() const override;
    QSet<QString> activeIds() const override;
    bool accepts(const QString& modId) const override;
    std::optional<QString> updatableId(const ListedMod& mod) const override;
    std::optional<double> lastSynced(const ListedMod& mod) const override;
    QCoro::Task<Result<DownloadResult>> downloadMods(QStringList modIds) override;
    void cancel() override;

    PluginConfig<SteamSettings>& settings() noexcept { return m_settings; }
    const PluginConfig<SteamSettings>& settings() const noexcept { return m_settings; }

    // Loads workshop_sync.json off the UI thread; called by init(), exposed for tests.
    QCoro::Task<void> loadSyncState();

    // Ids never synced, or last synced more than `max_age_s` before `now` (unix seconds),
    // oldest first.
    QStringList staleIds(double maxAgeS, double now) const;

    // One auto-update pass: downloads the stale mods when auto-update is enabled and nothing
    // is running. The timer started by init() runs this.
    QCoro::Task<void> autoUpdateOnce();

private:
    // The per-item results of one PxSteamDL call, or the error text if the call itself failed.
    using Outcome = std::expected<std::vector<pxsteamdl::Result>, std::string>;

    QString localPath() const;
    std::optional<double> syncedAt(const QString& modId) const;
    LoginOptions wantedOptions() const;
    QCoro::Task<Result<std::shared_ptr<WorkshopClient>>> ensureClient();

    QCoro::Task<void> runBatch(QStringList ids, QString root, std::shared_ptr<SteamBatch> batch);
    DownloadResult finishBatch(const std::shared_ptr<SteamBatch>& batch);
    void finishItem(SteamBatch& batch, const QString& modId, const QString& error);
    void recordSynced(const QStringList& ids);

    void onResolved(SteamBatch& batch, const QString& modId, const QString& title);
    void onProgress(SteamBatch& batch, const QString& modId, qint64 done, qint64 total);
    void emitItemProgress(SteamBatch& batch, const QString& modId, qint64 done, qint64 total);

    void armAutoUpdate(std::chrono::milliseconds delay);
    QCoro::Task<void> runScheduledAutoUpdate();

    ConfigService& m_config;
    std::function<QString()> m_localModsPath;
    std::function<QList<ListedModPtr>()> m_installedMods;
    ClientFactory m_clientFactory;
    PluginConfig<SteamSettings> m_settings;
    WorkshopSyncState m_sync;

    std::shared_ptr<WorkshopClient> m_client;
    LoginOptions m_clientOptions;

    // Set while a batch is downloading.
    std::optional<std::stop_source> m_stop;
    QSet<QString> m_activeIds;
    // The PxSteamDL call of the running batch, so shutdown() can wait for it.
    QFuture<Outcome> m_running;

    // Worker-thread callbacks are posted to this object, which lives on the main thread.
    QObject m_dispatch;
    QTimer m_autoTimer;
    bool m_stopping = false;
};

}  // namespace px::modrim

#endif  // PX_HAS_STEAM
