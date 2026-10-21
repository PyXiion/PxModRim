#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

#include <QCoro/QCoroTask>

#include "core/error.hpp"
#include "core/services/startup_impact_service/db.hpp"
#include "core/services/startup_impact_service/models.hpp"

namespace px::modrim {
class CoreContext;
}

namespace px::modrim::startup_impact {

// Loads the game's startup impact report into the history database and answers
// queries about historical averages and per-session breakdowns.
class StartupImpactService {
 public:
  // `ctx` is not owned and must outlive the service.
  explicit StartupImpactService(CoreContext& ctx);

  // Imports the report from the game's config folder and records it in history.
  // Resolves to nullopt when no config folder is set or no report can be read.
  QCoro::Task<Result<std::optional<StartupImpactReport>>> load();

  QCoro::Task<Result<QHash<QString, double>>> getAllAverages();
  QCoro::Task<Result<LatestWithAverages>> snapshot(
      QStringList activePids, std::optional<QString> selectedPid = {});
  QCoro::Task<Result<void>> clear();

  QCoro::Task<void> closeConnection();
  void closeConnectionSync();

 private:
  CoreContext& m_ctx;
  StartupImpactDb m_db;
};

}  // namespace px::modrim::startup_impact
