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

#include "core/config.h"
#include "core/downloads/downloader.h"
#include "core/downloads/types.h"
#include "core/error.h"
#include "core/models/metadata/structures.h"
#include "core/plugin_config.h"

namespace px::core {

// PxSteamDL runs one shared pool of parallel_items * threads_per_item workers.
inline constexpr int kSteamMaxParallelItems = 8;
inline constexpr int kSteamMaxThreadsPerItem = 16;

inline constexpr std::chrono::seconds kAutoUpdateStartupDelay{45};
inline constexpr std::chrono::seconds kAutoUpdateCheckInterval{600};

// In the config folder; records when each Workshop item was last synced.
inline const QString kWorkshopSyncFile = QStringLiteral("workshop_sync.json");

struct SteamSettings {
    int auto_update_hours = 0;
    int parallel_items = 8;
    int threads_per_item = 1;
    std::string proxy;
    int connect_timeout = 10;
    int stall_timeout = 30;

    bool operator==(const SteamSettings&) const = default;
};

// Unix seconds of the last sync, keyed by Workshop id.
struct WorkshopSyncState {
    std::map<std::string, double> synced;
};

// Network settings of one Steam login. An unset proxy means libcurl's environment.
struct LoginOptions {
    std::optional<std::string> proxy;
    int connect_timeout = 10;
    int stall_timeout = 30;

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
QCoro::Task<Result<std::shared_ptr<WorkshopClient>>> login_with_pxsteamdl(LoginOptions options);

// Progress of one download batch. An item counts as complete once all its bytes are in,
// which can happen before PxSteamDL returns its Result.
struct SteamBatch {
    explicit SteamBatch(int total_items);

    int total = 0;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point last_logged;
    std::chrono::steady_clock::time_point last_emitted;
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
    std::function<QString()> local_mods_path;
    // Mods the application knows about; auto-update picks the stale ones among them.
    std::function<QList<ListedModPtr>()> installed_mods;
    ClientFactory client_factory = login_with_pxsteamdl;
};

// Downloads Steam Workshop items into the local mods folder via PxSteamDL.
class SteamDownloader : public Downloader {
public:
    explicit SteamDownloader(SteamDownloaderDeps deps);

    QCoro::Task<void> init(CoreContext& ctx) override;
    QCoro::Task<void> shutdown() override;

    bool is_downloading() const override;
    QSet<QString> active_ids() const override;
    bool accepts(const QString& mod_id) const override;
    std::optional<QString> updatable_id(const ListedMod& mod) const override;
    std::optional<double> last_synced(const ListedMod& mod) const override;
    QCoro::Task<Result<DownloadResult>> download_mods(QStringList mod_ids) override;
    void cancel() override;

    PluginConfig<SteamSettings>& settings() noexcept { return settings_; }
    const PluginConfig<SteamSettings>& settings() const noexcept { return settings_; }

    // Loads workshop_sync.json off the UI thread; called by init(), exposed for tests.
    QCoro::Task<void> load_sync_state();

    // Ids never synced, or last synced more than `max_age_s` before `now` (unix seconds),
    // oldest first.
    QStringList stale_ids(double max_age_s, double now) const;

    // One auto-update pass: downloads the stale mods when auto-update is enabled and nothing
    // is running. The timer started by init() runs this.
    QCoro::Task<void> auto_update_once();

private:
    // The per-item results of one PxSteamDL call, or the error text if the call itself failed.
    using Outcome = std::expected<std::vector<pxsteamdl::Result>, std::string>;

    QString local_path() const;
    std::optional<double> synced_at(const QString& mod_id) const;
    LoginOptions wanted_options() const;
    QCoro::Task<Result<std::shared_ptr<WorkshopClient>>> ensure_client();

    QCoro::Task<void> run_batch(QStringList ids, QString root, std::shared_ptr<SteamBatch> batch);
    DownloadResult finish_batch(const std::shared_ptr<SteamBatch>& batch);
    void finish_item(SteamBatch& batch, const QString& mod_id, const QString& error);
    void record_synced(const QStringList& ids);

    void on_resolved(SteamBatch& batch, const QString& mod_id, const QString& title);
    void on_progress(SteamBatch& batch, const QString& mod_id, qint64 done, qint64 total);
    void emit_item_progress(SteamBatch& batch, const QString& mod_id, qint64 done, qint64 total);

    void arm_auto_update(std::chrono::milliseconds delay);
    QCoro::Task<void> run_scheduled_auto_update();

    ConfigService& config_;
    std::function<QString()> local_mods_path_;
    std::function<QList<ListedModPtr>()> installed_mods_;
    ClientFactory client_factory_;
    PluginConfig<SteamSettings> settings_;
    WorkshopSyncState sync_;

    std::shared_ptr<WorkshopClient> client_;
    LoginOptions client_options_;

    // Set while a batch is downloading.
    std::optional<std::stop_source> stop_;
    QSet<QString> active_ids_;
    // The PxSteamDL call of the running batch, so shutdown() can wait for it.
    QFuture<Outcome> running_;

    // Worker-thread callbacks are posted to this object, which lives on the main thread.
    QObject dispatch_;
    QTimer auto_timer_;
    bool stopping_ = false;
};

}  // namespace px::core

#endif  // PX_HAS_STEAM
