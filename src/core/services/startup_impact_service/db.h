#pragma once

#include <optional>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include "core/error.h"
#include "core/services/startup_impact_service/models.h"

namespace px::core::startup_impact {

// Average on-thread and off-thread seconds for one package id.
struct ThreadAverages {
    double on_thread = 0.0;
    double off_thread = 0.0;
};

struct LatestWithAverages {
    std::optional<StartupImpactReport> report;
    double base_game_avg = 0.0;
    // Keyed by every requested package id; ids without history map to zeros.
    QHash<QString, ThreadAverages> totals;
    double selected_avg = 0.0;
};

// `cache.db` in the config directory. Shared with the metadata cache, which owns other tables.
QString db_path(const QString& config_dir);

// Startup impact history in SQLite, kept to the most recent sessions. Averages weight newer
// sessions more heavily. The schema matches the legacy Python app so either can read the file.
//
// All database work runs on one worker thread, which owns the single connection. Calls may
// therefore be made from the Qt event loop without blocking it, and need no further locking.
class StartupImpactDb {
public:
    // The open connection, identified by a name unique to this instance. Only the worker
    // thread reads or writes it.
    struct Connection {
        QString name;
        QString path;
        bool open = false;
    };

    StartupImpactDb();
    ~StartupImpactDb();
    StartupImpactDb(const StartupImpactDb&) = delete;
    StartupImpactDb& operator=(const StartupImpactDb&) = delete;

    QCoro::Task<Result<void>> store_report(QString path, StartupImpactReport report);
    QCoro::Task<Result<std::optional<StartupImpactReport>>> get_latest_report(QString path);
    QCoro::Task<Result<double>> get_average(QString path, QString package_id);
    QCoro::Task<Result<QHash<QString, ThreadAverages>>> get_totals_for(QString path,
                                                                        QStringList package_ids);
    QCoro::Task<Result<QHash<QString, double>>> get_all_averages(QString path);
    QCoro::Task<Result<LatestWithAverages>> get_latest_with_averages(
        QString path, QStringList active_pids, std::optional<QString> selected_pid);
    QCoro::Task<Result<void>> clear(QString path);

    QCoro::Task<void> close();
    // For shutdown paths that cannot await. Blocks until the connection is closed.
    void close_sync();

private:
    // The destructor drains the pool through close_sync() before these members are destroyed.
    QThreadPool pool_;
    Connection conn_;
};

}  // namespace px::core::startup_impact
