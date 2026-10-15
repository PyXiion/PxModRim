#include "core/services/startup_impact_service/service.h"

#include <algorithm>
#include <numeric>

#include <spdlog/spdlog.h>

#include "core/config.h"
#include "core/context.h"
#include "core/services/startup_impact_service/parser.h"

namespace px::core::startup_impact {

namespace {

constexpr double kMinBaseGameTimeS = 0.001;

QString dbPathFor(CoreContext& ctx) { return db_path(ctx.configService().directory()); }

QStringList normalized(const QStringList& pids) {
    QStringList out;
    out.reserve(pids.size());
    for (const QString& pid : pids) out.push_back(normalize_package_id(pid));
    return out;
}

}  // namespace

StartupImpactService::StartupImpactService(CoreContext& ctx) : ctx_(ctx) {}

QCoro::Task<Result<std::optional<StartupImpactReport>>> StartupImpactService::load() {
    const std::string& configFolder = ctx_.config().paths.config_folder;
    if (configFolder.empty()) co_return std::optional<StartupImpactReport>{};

    const QString dbPath = dbPathFor(ctx_);
    const QString jsonPath = get_startup_impact_path(QString::fromStdString(configFolder));
    spdlog::debug("Loading startup impact from {}", jsonPath.toStdString());

    std::optional<StartupImpactReport> report = parse_startup_impact(jsonPath);
    if (report) {
        const double base = std::max(0.0, report->base_game_time());
        QList<StartupImpactMod> entries = report->mods;
        if (base > kMinBaseGameTimeS) {
            StartupImpactMod baseGame;
            baseGame.mod_name = QString(BASE_GAME_PID);
            baseGame.package_id = QString(BASE_GAME_PID);
            baseGame.total_impact_s = base;
            baseGame.metrics = report->metrics;
            baseGame.off_thread_metrics = report->off_thread_metrics;
            baseGame.off_thread_total_impact_s =
                std::accumulate(report->off_thread_metrics.cbegin(),
                                report->off_thread_metrics.cend(), 0.0);
            entries.push_back(std::move(baseGame));
        }
        StartupImpactReport full;
        full.path = report->path;
        full.loading_time_s = report->loading_time_s;
        full.mods = std::move(entries);
        full.timestamp = report->timestamp;
        auto stored = co_await db_.store_report(dbPath, std::move(full));
        if (!stored) co_return std::unexpected(stored.error());
    }
    co_return report;
}

QCoro::Task<Result<std::optional<StartupImpactReport>>> StartupImpactService::get_latest_report() {
    co_return co_await db_.get_latest_report(dbPathFor(ctx_));
}

QCoro::Task<Result<double>> StartupImpactService::estimated_impact_for(QString package_id) {
    co_return co_await db_.get_average(dbPathFor(ctx_), normalize_package_id(package_id));
}

QCoro::Task<Result<double>> StartupImpactService::estimated_total(QStringList active_pids) {
    const QString dbPath = dbPathFor(ctx_);
    auto base = co_await db_.get_average(dbPath, QString(BASE_GAME_PID));
    if (!base) co_return std::unexpected(base.error());
    auto totals = co_await db_.get_totals_for(dbPath, normalized(active_pids));
    if (!totals) co_return std::unexpected(totals.error());
    double modSum = 0.0;
    for (const ThreadAverages& t : *totals) modSum += t.on_thread + t.off_thread;
    co_return *base + modSum;
}

QCoro::Task<Result<QHash<QString, double>>> StartupImpactService::get_all_averages() {
    co_return co_await db_.get_all_averages(dbPathFor(ctx_));
}

QCoro::Task<Result<double>> StartupImpactService::base_game_average() {
    co_return co_await db_.get_average(dbPathFor(ctx_), QString(BASE_GAME_PID));
}

QCoro::Task<Result<LatestWithAverages>> StartupImpactService::snapshot(
    QStringList active_pids, std::optional<QString> selected_pid) {
    std::optional<QString> selected;
    if (selected_pid && !selected_pid->isEmpty()) selected = normalize_package_id(*selected_pid);
    co_return co_await db_.get_latest_with_averages(dbPathFor(ctx_), normalized(active_pids),
                                                    std::move(selected));
}

QCoro::Task<Result<void>> StartupImpactService::clear() {
    co_return co_await db_.clear(dbPathFor(ctx_));
}

QCoro::Task<void> StartupImpactService::close_connection() { co_await db_.close(); }

void StartupImpactService::close_connection_sync() { db_.close_sync(); }

}  // namespace px::core::startup_impact
