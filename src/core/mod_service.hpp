#pragma once

#include <memory>
#include <optional>
#include <variant>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>

#include "core/error.hpp"
#include "core/events.hpp"
#include "core/profiler.hpp"
#include "core/providers/factory.hpp"
#include "core/services/metadata_cache.hpp"
#include "core/services/startup_impact_service/service.hpp"

namespace px::modrim {

class CoreContext;

// Orchestrates mod discovery and lifecycle across a set of providers.
class ModService {
public:
    // `ctx` is not owned and must outlive the service. Without a `metadata_cache`, one is created
    // in the config folder when the context has a config service.
    ModService(CoreContext& ctx, providers::ProviderList providers,
               std::unique_ptr<services::MetadataCache> metadataCache = nullptr);

    startup_impact::StartupImpactService& startupImpact() noexcept { return m_startupImpact; }

    // Replaces the provider set.
    void resetProviders(providers::ProviderList providers);
    // The provider with `provider_id`, or nullptr.
    providers::BaseModProvider* getProvider(const QString& providerId) const;
    QHash<QString, QString> providerColors() const;

    // Runs all providers in parallel and loads the results into the context. `timer` is optional;
    // without one, the profile report goes to the debug log.
    QCoro::Task<Result<void>> discover(Profiler* timer = nullptr, bool forceReparse = false);

    // One-time startup work: snapshot the mod list, discover, initialize diagnostics, load impact.
    QCoro::Task<Result<void>> initialize();
    // Reparses About.xml files when `full` is set.
    QCoro::Task<Result<void>> reload(bool full = false);

    // Available mod-list snapshots, newest first.
    QStringList getSnapshots() const;
    // Restores a snapshot to ModsConfig.xml and reloads live state.
    QCoro::Task<bool> restoreSnapshot(QString snapshotPath);
    // Persists the given active UUIDs as ModsConfig.xml.
    QCoro::Task<bool> saveActiveLayout(QStringList activeIds);

    QCoro::Task<void> close();
    // Synchronous teardown for use after the event loop has stopped.
    void closeSync();

    Event<std::monostate> modsChanged;

private:
    QCoro::Task<Result<providers::ModMap>> discoverOne(providers::BaseModProvider& provider,
                                                        Profiler& timer, bool forceReparse);
    QCoro::Task<Result<void>> runRefreshPipeline(const QString& label, bool forceReparse);
    void snapshotCurrentConfig();
    std::optional<QString> snapshotsDir() const;

    CoreContext& m_ctx;
    providers::ProviderList m_providers;
    std::unique_ptr<services::MetadataCache> m_metadataCache;
    startup_impact::StartupImpactService m_startupImpact;
    // Serializes ModsConfig.xml writes; they run on the thread pool.
    mutable QMutex m_saveMutex;
};

}  // namespace px::modrim
