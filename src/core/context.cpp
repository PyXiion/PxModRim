#include "core/context.hpp"

#include <QSet>
#include <chrono>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/downloads/downloader.hpp"
#include "core/downloads/manager.hpp"
#include "core/mod_service.hpp"
#include "core/profiler.hpp"
#include "core/providers/factory.hpp"
#include "core/services/activation_service.hpp"
#include "core/services/diagnostics_service.hpp"
#include "core/services/game_launcher.hpp"
#include "core/services/sort_service.hpp"

namespace px::modrim {

namespace {

// Minimum valid version string shape: "X.Y" — reject anything shorter.
constexpr qsizetype kVersionMinParts = 2;

}  // namespace

CoreContext::CoreContext(AppConfig cfg, ConfigService* configService)
    : m_cfg(std::move(cfg)), m_configService(configService) {
  refreshGameVersion();
  m_plugins.registerPlugin(std::make_unique<DownloadManager>());
}

CoreContext::~CoreContext() = default;

std::unique_ptr<CoreContext> CoreContext::create(AppConfig cfg,
                                                 ConfigService* configService) {
  std::unique_ptr<CoreContext> ctx(
      new CoreContext(std::move(cfg), configService));
  ProfileSession session(QStringLiteral("services.create"));
  Profiler& timer = session.timer();
  {
    auto scope = timer(QStringLiteral("create_providers"));
    ctx->m_modService = std::make_unique<ModService>(
        *ctx, providers::createProviders(ctx->m_cfg.paths, &ctx->m_pool));
  }
  {
    auto scope = timer(QStringLiteral("diagnostics_service"));
    ctx->m_diagnosticsService =
        std::make_unique<DiagnosticsService>(*ctx, ctx->m_network);
  }
  {
    auto scope = timer(QStringLiteral("sort_service"));
    ctx->m_sortService =
        std::make_unique<SortService>(*ctx, *ctx->m_diagnosticsService);
  }
  {
    auto scope = timer(QStringLiteral("game_launcher"));
    ctx->m_gameLauncher = std::make_unique<services::GameLauncher>(*ctx);
  }
  {
    auto scope = timer(QStringLiteral("activation"));
    ctx->m_activation = std::make_unique<ActivationService>(*ctx);
  }
  return ctx;
}

void CoreContext::load(QHash<QString, ListedModPtr> mods,
                       QStringList activeUuids) {
  m_mods = std::move(mods);
  m_loadedActiveUuids = activeUuids;
  m_activeUuids = std::move(activeUuids);
}

void CoreContext::setActive(QStringList uuids) {
  if (m_activeUuids == uuids) {
    return;
  }
  const QSet<QString> oldSet(m_activeUuids.cbegin(), m_activeUuids.cend());
  const QSet<QString> newSet(uuids.cbegin(), uuids.cend());
  const bool setChanged = oldSet != newSet;
  m_activeUuids = std::move(uuids);
  if (setChanged) {
    m_diagnosticsService->rebuild(m_activeUuids);
  } else {
    m_diagnosticsService->reorder(m_activeUuids);
  }
  m_activeStateChanged.notify(m_activeUuids);
}

QCoro::Task<std::pair<int, double>> CoreContext::autoSort() {
  const QSet<QString> active(m_activeUuids.cbegin(), m_activeUuids.cend());
  const QStringList deps = m_sortService->resolveMissingDependencies(active);
  if (!deps.isEmpty()) {
    m_activeUuids.append(deps);
    m_diagnosticsService->rebuild(m_activeUuids);
    m_activeStateChanged.notify(m_activeUuids);
    spdlog::info("auto-sort: enabling {} missing dependencies", deps.size());
  }

  const auto started = std::chrono::steady_clock::now();
  m_activeUuids = co_await m_sortService->sortActiveMods();
  const double elapsedMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  m_diagnosticsService->reorder(m_activeUuids);
  m_activeStateChanged.notify(m_activeUuids);
  const int count = static_cast<int>(m_activeUuids.size());
  spdlog::info("auto-sort: {} mods sorted in {:.0f}ms", count, elapsedMs);
  co_return std::pair{count, elapsedMs};
}

void CoreContext::updateConfig(AppConfig cfg) {
  m_cfg = std::move(cfg);
  refreshGameVersion();
  m_configChanged.notify(m_cfg);
}

void CoreContext::resetProviders(const PathConfig& paths) {
  m_modService->resetProviders(providers::createProviders(paths, &m_pool));
}

void CoreContext::refreshGameVersion() {
  if (m_cfg.paths.game.empty()) {
    m_gameVersion = QStringLiteral("Unknown");
    return;
  }
  const auto version =
      readGameVersion(QString::fromStdString(m_cfg.paths.game));
  m_gameVersion =
      version && !version->isEmpty() ? *version : QStringLiteral("Unknown");
}

QString CoreContext::targetVersion() const {
  const QStringList parts = m_gameVersion.split(QLatin1Char('.'));
  if (parts.size() >= kVersionMinParts) {
    return QStringLiteral("%1.%2").arg(parts[0], parts[1]);
  }
  return QStringLiteral("Unknown");
}

CollectionStats CoreContext::computeStats(
    const std::optional<QStringList>& activeIds) const {
  const QStringList& ids = activeIds ? *activeIds : m_activeUuids;
  CollectionStats stats;
  stats.total = m_mods.size();
  stats.active = ids.size();
  stats.inactive = stats.total - stats.active;
  for (const auto& mod : m_mods) {
    if (!mod->valid) {
      ++stats.errors;
    }
  }
  return stats;
}

ConfigService& CoreContext::configService() const {
  Q_ASSERT(m_configService != nullptr);
  return *m_configService;
}

DownloadManager& CoreContext::downloads() const {
  auto* manager =
      dynamic_cast<DownloadManager*>(m_plugins.get(kDownloadsManagerName));
  Q_ASSERT(manager != nullptr);
  return *manager;
}

QCoro::Task<Result<void>> CoreContext::initialize() {
  co_return co_await m_modService->initialize();
}

QCoro::Task<void> CoreContext::close() {
  m_pool.waitForDone();
  co_await m_modService->startupImpact().closeConnection();
  co_await m_modService->close();
}

void CoreContext::closeSync() {
  m_pool.waitForDone();
  m_modService->startupImpact().closeConnectionSync();
  m_modService->closeSync();
}

void CoreContext::registerPlugin(std::unique_ptr<Plugin> plugin) {
  m_plugins.registerPlugin(std::move(plugin));
}

}  // namespace px::modrim
