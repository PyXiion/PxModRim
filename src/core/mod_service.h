#pragma once

#include <memory>
#include <optional>
#include <variant>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>

#include "core/error.h"
#include "core/events.h"
#include "core/profiler.h"
#include "core/providers/factory.h"
#include "core/services/metadata_cache.h"
#include "core/services/startup_impact_service/service.h"

namespace px::modrim {

class CoreContext;

// Orchestrates mod discovery and lifecycle across a set of providers.
class ModService {
public:
    // `ctx` is not owned and must outlive the service. Without a `metadata_cache`, one is created
    // in the config folder when the context has a config service.
    ModService(CoreContext& ctx, providers::ProviderList providers,
               std::unique_ptr<services::MetadataCache> metadata_cache = nullptr);

    startup_impact::StartupImpactService& startup_impact() noexcept { return startup_impact_; }

    // Replaces the provider set.
    void reset_providers(providers::ProviderList providers);
    // The provider with `provider_id`, or nullptr.
    providers::BaseModProvider* get_provider(const QString& provider_id) const;
    QHash<QString, QString> provider_colors() const;

    // Runs all providers in parallel and loads the results into the context. `timer` is optional;
    // without one, the profile report goes to the debug log.
    QCoro::Task<Result<void>> discover(Profiler* timer = nullptr, bool force_reparse = false);

    // One-time startup work: snapshot the mod list, discover, initialize diagnostics, load impact.
    QCoro::Task<Result<void>> initialize();
    // Reparses About.xml files when `full` is set.
    QCoro::Task<Result<void>> reload(bool full = false);

    // Available mod-list snapshots, newest first.
    QStringList get_snapshots() const;
    // Restores a snapshot to ModsConfig.xml and reloads live state.
    QCoro::Task<bool> restore_snapshot(QString snapshot_path);
    // Persists the given active UUIDs as ModsConfig.xml.
    QCoro::Task<bool> save_active_layout(QStringList active_ids);

    QCoro::Task<void> close();
    // Synchronous teardown for use after the event loop has stopped.
    void close_sync();

    Event<std::monostate> mods_changed;

private:
    QCoro::Task<Result<providers::ModMap>> discover_one(providers::BaseModProvider& provider,
                                                        Profiler& timer, bool force_reparse);
    QCoro::Task<Result<void>> run_refresh_pipeline(const QString& label, bool force_reparse);
    void snapshot_current_config();
    std::optional<QString> snapshots_dir() const;

    CoreContext& ctx_;
    providers::ProviderList providers_;
    std::unique_ptr<services::MetadataCache> metadata_cache_;
    startup_impact::StartupImpactService startup_impact_;
    // Serializes ModsConfig.xml writes; they run on the thread pool.
    mutable QMutex save_mutex_;
};

}  // namespace px::modrim
