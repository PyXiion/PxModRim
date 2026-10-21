#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <optional>

#include <QCoro/QCoroTask>

#include "core/error.hpp"
#include "core/services/startup_impact_service/models.hpp"

namespace px::modrim::startup_impact {

// Average on-thread and off-thread seconds for one package id.
struct ThreadAverages {
  double onThread = 0.0;
  double offThread = 0.0;
};

struct LatestWithAverages {
  std::optional<StartupImpactReport> report;
  double baseGameAvg = 0.0;
  // Keyed by every requested package id; ids without history map to zeros.
  QHash<QString, ThreadAverages> totals;
  double selectedAvg = 0.0;
};

// `cache.db` in the config directory. Shared with the metadata cache, which
// owns other tables.
QString dbPath(const QString& configDir);

// Startup impact history in SQLite, kept to the most recent sessions. Averages
// weight newer sessions more heavily. The schema matches the legacy Python app
// so either can read the file.
//
// All database work runs on one worker thread, which owns the single
// connection. Calls may therefore be made from the Qt event loop without
// blocking it, and need no further locking.
class StartupImpactDb {
 public:
  // The open connection, identified by a name unique to this instance. Only the
  // worker thread reads or writes it.
  struct Connection {
    QString name;
    QString path;
    bool open = false;
  };

  StartupImpactDb();
  ~StartupImpactDb();
  StartupImpactDb(const StartupImpactDb&) = delete;
  StartupImpactDb& operator=(const StartupImpactDb&) = delete;

  QCoro::Task<Result<void>> storeReport(QString path,
                                        StartupImpactReport report);
  QCoro::Task<Result<std::optional<StartupImpactReport>>> getLatestReport(
      QString path);
  QCoro::Task<Result<QHash<QString, double>>> getAllAverages(QString path);
  QCoro::Task<Result<LatestWithAverages>> getLatestWithAverages(
      QString path, QStringList activePids, std::optional<QString> selectedPid);
  QCoro::Task<Result<void>> clear(QString path);

  QCoro::Task<void> close();
  // For shutdown paths that cannot await. Blocks until the connection is
  // closed.
  void closeSync();

 private:
  // The destructor drains the pool through closeSync() before these members
  // are destroyed.
  QThreadPool m_pool;
  Connection m_conn;
};

}  // namespace px::modrim::startup_impact
