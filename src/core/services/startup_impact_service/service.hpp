#pragma once

#include <optional>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QString>
#include <QStringList>

#include "core/error.hpp"
#include "core/services/startup_impact_service/db.hpp"
#include "core/services/startup_impact_service/models.hpp"

namespace px::modrim {
class CoreContext;
}

namespace px::modrim::startup_impact {

// Loads the game's startup impact report into the history database and answers queries about
// historical averages and per-session breakdowns.
class StartupImpactService {
public:
    // `ctx` is not owned and must outlive the service.
    explicit StartupImpactService(CoreContext& ctx);

    // Imports the report from the game's config folder and records it in history. Resolves to
    // nullopt when no config folder is set or no report can be read.
    QCoro::Task<Result<std::optional<StartupImpactReport>>> load();

    QCoro::Task<Result<std::optional<StartupImpactReport>>> get_latest_report();
    QCoro::Task<Result<double>> estimated_impact_for(QString package_id);
    // Base-game average plus the on-thread and off-thread averages of each active mod.
    QCoro::Task<Result<double>> estimated_total(QStringList active_pids);
    QCoro::Task<Result<QHash<QString, double>>> get_all_averages();
    QCoro::Task<Result<double>> base_game_average();
    QCoro::Task<Result<LatestWithAverages>> snapshot(QStringList active_pids,
                                                     std::optional<QString> selected_pid = {});
    QCoro::Task<Result<void>> clear();

    QCoro::Task<void> close_connection();
    void close_connection_sync();

private:
    CoreContext& ctx_;
    StartupImpactDb db_;
};

}  // namespace px::modrim::startup_impact
