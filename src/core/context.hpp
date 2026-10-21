#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QThreadPool>
#include <memory>
#include <optional>
#include <utility>

#include <QCoro/QCoroTask>

#include "core/config.hpp"
#include "core/error.hpp"
#include "core/events.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/plugin.hpp"

namespace px::modrim {

class ActivationService;
class ConfigService;
class DiagnosticsService;
class DownloadManager;
class ModService;
class SortService;

namespace services {
class GameLauncher;
}

// Centralised application state: mods, active load order, config and game
// version, plus the services that act on them. Services hold a reference back
// to the context, so it is built only through create() and never moved.
class CoreContext {
 public:
  // `config_service` is not owned and must outlive the context.
  static std::unique_ptr<CoreContext> create(
      AppConfig cfg, ConfigService* configService = nullptr);

  ~CoreContext();
  CoreContext(const CoreContext&) = delete;
  CoreContext& operator=(const CoreContext&) = delete;
  CoreContext(CoreContext&&) = delete;
  CoreContext& operator=(CoreContext&&) = delete;

  // Replaces all mods and the active list, taking ownership of the data.
  void load(QHash<QString, ListedModPtr> mods, QStringList activeUuids);
  void setActive(QStringList uuids);
  // Enables missing dependencies, then sorts the active mods. Resolves to the
  // number of active mods and the time the sort took in milliseconds.
  QCoro::Task<std::pair<int, double>> autoSort();
  // Replaces the live config and refreshes derived values (game version).
  void updateConfig(AppConfig cfg);
  // Rebuilds the providers for new paths.
  void resetProviders(const PathConfig& paths);

  QCoro::Task<void> close();
  // Synchronous teardown for use after the event loop has stopped.
  void closeSync();

  void registerPlugin(std::unique_ptr<Plugin> plugin);

  const AppConfig& config() const noexcept { return m_cfg; }
  const QHash<QString, ListedModPtr>& allMods() const noexcept {
    return m_mods;
  }
  const QStringList& activeUuids() const noexcept { return m_activeUuids; }
  // Active UUIDs as last loaded from disk, unaffected by later edits.
  const QStringList& loadedActiveUuids() const noexcept {
    return m_loadedActiveUuids;
  }
  // "Unknown" when the game is not configured or its version cannot be read.
  const QString& gameVersion() const noexcept { return m_gameVersion; }
  // The "X.Y" part of the game version, or "Unknown".
  QString targetVersion() const;

  Event<QStringList>& activeStateChanged() noexcept {
    return m_activeStateChanged;
  }
  Event<AppConfig>& configChanged() noexcept { return m_configChanged; }

  bool hasConfigService() const noexcept { return m_configService != nullptr; }
  ConfigService& configService() const;
  QNetworkAccessManager& network() noexcept { return m_network; }

  ModService& modService() noexcept { return *m_modService; }
  DiagnosticsService& diagnosticsService() noexcept {
    return *m_diagnosticsService;
  }
  SortService& sortService() noexcept { return *m_sortService; }
  ActivationService& activation() noexcept { return *m_activation; }
  services::GameLauncher& gameLauncher() noexcept { return *m_gameLauncher; }
  DownloadManager& downloads() const;
  PluginRegistry& plugins() noexcept { return m_plugins; }

 private:
  CoreContext(AppConfig cfg, ConfigService* configService);
  void refreshGameVersion();

  AppConfig m_cfg;
  ConfigService* m_configService;
  QHash<QString, ListedModPtr> m_mods;
  QStringList m_activeUuids;
  QStringList m_loadedActiveUuids;
  QString m_gameVersion;
  Event<QStringList> m_activeStateChanged;
  Event<AppConfig> m_configChanged;
  // Shared by providers and the network; declared before the services so they
  // outlive them.
  QThreadPool m_pool;
  QNetworkAccessManager m_network;
  // Services are declared before the plugins, so plugins (which disconnect from
  // service events in their destructors) are destroyed first.
  std::unique_ptr<ModService> m_modService;
  std::unique_ptr<DiagnosticsService> m_diagnosticsService;
  std::unique_ptr<SortService> m_sortService;
  std::unique_ptr<services::GameLauncher> m_gameLauncher;
  std::unique_ptr<ActivationService> m_activation;
  PluginRegistry m_plugins;
};

}  // namespace px::modrim
