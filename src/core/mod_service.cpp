#include "core/mod_service.hpp"

#include <QDir>
#include <QSet>
#include <algorithm>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/context.hpp"
#include "core/mods_config.hpp"
#include "core/services/diagnostics_service.hpp"
#include "core/services/mod_discovery.hpp"

namespace px::modrim {

namespace {

QString configFolderOf(const CoreContext& ctx) {
  return QString::fromStdString(ctx.config().paths.config_folder);
}

}  // namespace

ModService::ModService(CoreContext& ctx, providers::ProviderList providers,
                       std::unique_ptr<services::MetadataCache> metadataCache)
    : m_ctx(ctx),
      m_providers(std::move(providers)),
      m_metadataCache(std::move(metadataCache)),
      m_startupImpact(ctx) {
  if (!m_metadataCache) {
    m_metadataCache = std::make_unique<services::MetadataCache>(
        services::metadataCachePath(ctx.configService().directory()));
  }
}

void ModService::resetProviders(providers::ProviderList providers) {
  m_providers = std::move(providers);
}

QHash<QString, QString> ModService::providerColors() const {
  QHash<QString, QString> colors;
  for (const auto& provider : m_providers) {
    colors.insert(provider->providerId(), provider->color());
  }
  return colors;
}

QCoro::Task<Result<providers::ModMap>> ModService::discoverOne(
    providers::BaseModProvider& provider, Profiler& timer, bool forceReparse) {
  auto scope = timer(QStringLiteral("provider.%1").arg(provider.providerId()));
  co_return co_await provider.discover(m_ctx.targetVersion(), &timer,
                                       m_metadataCache.get(), forceReparse);
}

QCoro::Task<Result<void>> ModService::discover(Profiler* timer,
                                               bool forceReparse) {
  // Each provider profiles into its own tree, since their discoveries
  // interleave.
  std::optional<Profiler> ownTimer;
  if (!timer) {
    timer = &ownTimer.emplace();
  }
  auto scope = (*timer)(QStringLiteral("discover"));

  std::vector<std::unique_ptr<Profiler>> childTimers;
  std::vector<QCoro::Task<Result<providers::ModMap>>> tasks;
  childTimers.reserve(m_providers.size());
  tasks.reserve(m_providers.size());
  for (const auto& provider : m_providers) {
    childTimers.push_back(std::make_unique<Profiler>());
    tasks.push_back(discoverOne(*provider, *childTimers.back(), forceReparse));
  }

  providers::ModMap allMods;
  std::optional<Error> failure;
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    auto result = co_await tasks[i];
    timer->adopt(*childTimers[i]);
    if (!result) {
      if (!failure) {
        failure = result.error();
      }
      continue;
    }
    for (auto it = result->cbegin(); it != result->cend(); ++it) {
      allMods.insert(it.key(), it.value());
    }
  }
  if (failure) {
    co_return std::unexpected(*failure);
  }

  if (m_metadataCache) {
    auto prune = (*timer)(QStringLiteral("cache_prune"));
    QStringList modPaths;
    for (const auto& mod : allMods) {
      if (mod->modPath()) {
        modPaths.append(*mod->modPath());
      }
    }
    const QStringList livePaths = co_await providers::runOnPool<QStringList>(
        nullptr, [modPaths] { return services::normalizePaths(modPaths); });
    co_await m_metadataCache->pruneDisappeared(
        QSet<QString>(livePaths.cbegin(), livePaths.cend()));
  }

  {
    auto resolve = (*timer)(QStringLiteral("resolve_active"));
    const QStringList active =
        services::resolveActiveUuids(allMods, configFolderOf(m_ctx));
    m_ctx.load(std::move(allMods), active);
  }

  if (ownTimer) {
    spdlog::debug("Profile report:\n{}", ownTimer->render().toStdString());
  }
  co_return {};
}

QCoro::Task<Result<void>> ModService::runRefreshPipeline(const QString& label,
                                                         bool forceReparse) {
  Profiler timer;
  auto scope = timer(QStringLiteral("mod_service.%1").arg(label));

  Profiler discoverTimer;
  Profiler diagnosticsTimer;
  auto discovery = discover(&discoverTimer, forceReparse);
  auto diagnostics = m_ctx.diagnosticsService().initialize(&diagnosticsTimer);
  auto impact = m_startupImpact.load();

  const auto discovered = co_await discovery;
  const auto initialized = co_await diagnostics;
  const auto loaded = co_await impact;
  timer.adopt(discoverTimer);
  timer.adopt(diagnosticsTimer);

  if (!discovered) {
    co_return std::unexpected(discovered.error());
  }
  if (!initialized) {
    co_return std::unexpected(initialized.error());
  }
  if (!loaded) {
    co_return std::unexpected(loaded.error());
  }
  m_ctx.diagnosticsService().rebuild(m_ctx.activeUuids(), &timer);
  spdlog::debug("Profile report:\n{}", timer.render().toStdString());
  modsChanged.notify(std::monostate{});
  co_return {};
}

QString ModService::snapshotsDir() const {
  return QDir(m_ctx.configService().directory())
      .filePath(QStringLiteral("snapshots"));
}

QCoro::Task<Result<void>> ModService::reload(bool full) {
  co_return co_await runRefreshPipeline(QStringLiteral("reload"), full);
}

QStringList ModService::getSnapshots() const {
  return px::modrim::getSnapshots(snapshotsDir());
}

QCoro::Task<bool> ModService::restoreSnapshot(QString snapshotPath) {
  const QString configFolder = configFolderOf(m_ctx);
  const QString snapshots = snapshotsDir();
  if (configFolder.isEmpty()) {
    spdlog::warn("Cannot restore snapshot: configuration is incomplete");
    co_return false;
  }

  const QString configPath = modsConfigPath(configFolder);
  auto restored = px::modrim::restoreSnapshot(
      snapshotPath, configPath, snapshots, m_ctx.config().max_snapshots);
  if (!restored) {
    spdlog::warn("Failed to restore snapshot: {}",
                 restored.error().message.toStdString());
    co_return false;
  }
  auto reloaded = co_await reload();
  if (!reloaded) {
    spdlog::error("Reload after snapshot restore failed: {}",
                  reloaded.error().message.toStdString());
    co_return false;
  }
  co_return true;
}

QCoro::Task<bool> ModService::saveActiveLayout(QStringList activeIds) {
  const QString configFolder = configFolderOf(m_ctx);
  if (configFolder.isEmpty()) {
    co_return false;
  }

  const auto& mods = m_ctx.allMods();
  QList<CaseInsensitiveStr> packageIds;
  for (const QString& uuid : activeIds) {
    if (auto about = std::dynamic_pointer_cast<AboutXmlMod>(mods.value(uuid))) {
      packageIds.append(about->packageId);
    }
  }
  const QString configPath = modsConfigPath(configFolder);
  const QString targetVersion = m_ctx.targetVersion();
  const int maxSnapshots = m_ctx.config().max_snapshots;
  const QString snapshotsPath = snapshotsDir();

  auto written = co_await providers::runOnPool<Result<void>>(
      nullptr, [this, configPath, snapshotsPath, targetVersion, maxSnapshots,
                packageIds = std::move(packageIds)] {
        QMutexLocker lock(&m_saveMutex);
        const auto existing = parseModsConfig(configPath);
        ModsConfig data;
        data.version = targetVersion;
        data.activeMods = packageIds;
        data.knownExpansions =
            existing ? existing->knownExpansions : QList<CaseInsensitiveStr>{};
        return writeModsConfig(configPath, data, snapshotsPath, maxSnapshots);
      });
  if (!written) {
    spdlog::warn("Failed to save ModsConfig.xml: {}",
                 written.error().message.toStdString());
    co_return false;
  }
  co_return true;
}

QCoro::Task<void> ModService::close() {
  if (m_metadataCache) {
    co_await m_metadataCache->close();
  }
}

void ModService::closeSync() {
  // Destroying the cache closes its connection and joins its worker thread.
  m_metadataCache.reset();
}

}  // namespace px::modrim
