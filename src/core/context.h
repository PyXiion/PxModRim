#pragma once

#include <memory>
#include <optional>
#include <utility>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QThreadPool>

#include "core/config.h"
#include "core/error.h"
#include "core/events.h"
#include "core/models/metadata/structures.h"
#include "core/plugin.h"
#include "core/structures.h"

namespace px::core {

class ActivationService;
class ConfigService;
class DiagnosticsService;
class DownloadManager;
class ModService;
class SortService;

namespace services {
class GameLauncher;
}

// Centralised application state: mods, active load order, config and game version, plus the
// services that act on them. Services hold a reference back to the context, so it is built only
// through create() and never moved.
class CoreContext {
public:
    // `config_service` is not owned and must outlive the context.
    static std::unique_ptr<CoreContext> create(AppConfig cfg,
                                               ConfigService* config_service = nullptr);

    ~CoreContext();
    CoreContext(const CoreContext&) = delete;
    CoreContext& operator=(const CoreContext&) = delete;
    CoreContext(CoreContext&&) = delete;
    CoreContext& operator=(CoreContext&&) = delete;

    // Replaces all mods and the active list, taking ownership of the data.
    void load(QHash<QString, ListedModPtr> mods, QStringList active_uuids);
    void setActive(QStringList uuids);
    // Enables missing dependencies, then sorts the active mods. Resolves to the number of active
    // mods and the time the sort took in milliseconds.
    QCoro::Task<std::pair<int, double>> autoSort();
    // Replaces the live config and refreshes derived values (game version).
    void updateConfig(AppConfig cfg);
    // Rebuilds the providers for new paths.
    void resetProviders(const PathConfig& paths);
    CollectionStats computeStats(const std::optional<QStringList>& active_ids = std::nullopt) const;

    QCoro::Task<Result<void>> initialize();
    QCoro::Task<void> close();
    // Synchronous teardown for use after the event loop has stopped.
    void closeSync();

    void registerPlugin(std::unique_ptr<Plugin> plugin);

    const AppConfig& config() const noexcept { return cfg_; }
    const QHash<QString, ListedModPtr>& allMods() const noexcept { return mods_; }
    const QStringList& activeUuids() const noexcept { return active_uuids_; }
    // Active UUIDs as last loaded from disk, unaffected by later edits.
    const QStringList& loadedActiveUuids() const noexcept { return loaded_active_uuids_; }
    // "Unknown" when the game is not configured or its version cannot be read.
    const QString& gameVersion() const noexcept { return game_version_; }
    // The "X.Y" part of the game version, or "Unknown".
    QString targetVersion() const;

    Event<QStringList>& activeStateChanged() noexcept { return active_state_changed_; }
    Event<AppConfig>& configChanged() noexcept { return config_changed_; }

    bool hasConfigService() const noexcept { return config_service_ != nullptr; }
    ConfigService& configService() const;
    QNetworkAccessManager& network() noexcept { return network_; }

    ModService& modService() noexcept { return *mod_service_; }
    DiagnosticsService& diagnosticsService() noexcept { return *diagnostics_service_; }
    SortService& sortService() noexcept { return *sort_service_; }
    ActivationService& activation() noexcept { return *activation_; }
    services::GameLauncher& gameLauncher() noexcept { return *game_launcher_; }
    DownloadManager& downloads() const;
    PluginRegistry& plugins() noexcept { return plugins_; }

private:
    CoreContext(AppConfig cfg, ConfigService* config_service);
    void refresh_game_version();

    AppConfig cfg_;
    ConfigService* config_service_;
    QHash<QString, ListedModPtr> mods_;
    QStringList active_uuids_;
    QStringList loaded_active_uuids_;
    QString game_version_;
    Event<QStringList> active_state_changed_;
    Event<AppConfig> config_changed_;
    // Shared by providers and the network; declared before the services so they outlive them.
    QThreadPool pool_;
    QNetworkAccessManager network_;
    // Services are declared before the plugins, so plugins (which disconnect from service
    // events in their destructors) are destroyed first.
    std::unique_ptr<ModService> mod_service_;
    std::unique_ptr<DiagnosticsService> diagnostics_service_;
    std::unique_ptr<SortService> sort_service_;
    std::unique_ptr<services::GameLauncher> game_launcher_;
    std::unique_ptr<ActivationService> activation_;
    PluginRegistry plugins_;
};

}  // namespace px::core
