#include "core/services/startup_impact_service/service.hpp"

#include <algorithm>
#include <numeric>

#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/context.hpp"
#include "core/services/startup_impact_service/parser.hpp"

namespace px::modrim::startup_impact {

namespace {

constexpr double kMinBaseGameTimeS = 0.001;

QString dbPathFor(CoreContext& ctx) {
  return dbPath(ctx.configService().directory());
}

QStringList normalized(const QStringList& pids) {
  QStringList out;
  out.reserve(pids.size());
  for (const QString& pid : pids) out.push_back(normalizePackageId(pid));
  return out;
}

}  // namespace

StartupImpactService::StartupImpactService(CoreContext& ctx) : m_ctx(ctx) {}

QCoro::Task<Result<std::optional<StartupImpactReport>>>
StartupImpactService::load() {
  const std::string& configFolder = m_ctx.config().paths.config_folder;
  if (configFolder.empty()) co_return std::optional<StartupImpactReport>{};

  const QString dbPath = dbPathFor(m_ctx);
  const QString jsonPath =
      getStartupImpactPath(QString::fromStdString(configFolder));
  spdlog::debug("Loading startup impact from {}", jsonPath.toStdString());

  std::optional<StartupImpactReport> report = parseStartupImpact(jsonPath);
  if (report) {
    const double base = std::max(0.0, report->baseGameTime());
    QList<StartupImpactMod> entries = report->mods;
    if (base > kMinBaseGameTimeS) {
      StartupImpactMod baseGame;
      baseGame.modName = QString(kBaseGamePid);
      baseGame.packageId = QString(kBaseGamePid);
      baseGame.totalImpactS = base;
      baseGame.metrics = report->metrics;
      baseGame.offThreadMetrics = report->offThreadMetrics;
      baseGame.offThreadTotalImpactS =
          std::accumulate(report->offThreadMetrics.cbegin(),
                          report->offThreadMetrics.cend(), 0.0);
      entries.push_back(std::move(baseGame));
    }
    StartupImpactReport full;
    full.path = report->path;
    full.loadingTimeS = report->loadingTimeS;
    full.mods = std::move(entries);
    full.timestamp = report->timestamp;
    auto stored = co_await m_db.storeReport(dbPath, std::move(full));
    if (!stored) co_return std::unexpected(stored.error());
  }
  co_return report;
}

QCoro::Task<Result<std::optional<StartupImpactReport>>>
StartupImpactService::getLatestReport() {
  co_return co_await m_db.getLatestReport(dbPathFor(m_ctx));
}

QCoro::Task<Result<double>> StartupImpactService::estimatedImpactFor(
    QString packageId) {
  co_return co_await m_db.getAverage(dbPathFor(m_ctx),
                                     normalizePackageId(packageId));
}

QCoro::Task<Result<double>> StartupImpactService::estimatedTotal(
    QStringList activePids) {
  const QString dbPath = dbPathFor(m_ctx);
  auto base = co_await m_db.getAverage(dbPath, QString(kBaseGamePid));
  if (!base) co_return std::unexpected(base.error());
  auto totals = co_await m_db.getTotalsFor(dbPath, normalized(activePids));
  if (!totals) co_return std::unexpected(totals.error());
  double modSum = 0.0;
  for (const ThreadAverages& t : *totals) modSum += t.onThread + t.offThread;
  co_return *base + modSum;
}

QCoro::Task<Result<QHash<QString, double>>>
StartupImpactService::getAllAverages() {
  co_return co_await m_db.getAllAverages(dbPathFor(m_ctx));
}

QCoro::Task<Result<double>> StartupImpactService::baseGameAverage() {
  co_return co_await m_db.getAverage(dbPathFor(m_ctx), QString(kBaseGamePid));
}

QCoro::Task<Result<LatestWithAverages>> StartupImpactService::snapshot(
    QStringList activePids, std::optional<QString> selectedPid) {
  std::optional<QString> selected;
  if (selectedPid && !selectedPid->isEmpty())
    selected = normalizePackageId(*selectedPid);
  co_return co_await m_db.getLatestWithAverages(
      dbPathFor(m_ctx), normalized(activePids), std::move(selected));
}

QCoro::Task<Result<void>> StartupImpactService::clear() {
  co_return co_await m_db.clear(dbPathFor(m_ctx));
}

QCoro::Task<void> StartupImpactService::closeConnection() {
  co_await m_db.close();
}

void StartupImpactService::closeConnectionSync() { m_db.closeSync(); }

}  // namespace px::modrim::startup_impact
