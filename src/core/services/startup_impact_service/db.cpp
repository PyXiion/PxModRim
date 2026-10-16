#include "core/services/startup_impact_service/db.h"

#include <initializer_list>

#include <QCoro/QCoroFuture>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include "core/migrator.h"

namespace px::modrim::startup_impact {

namespace {

// Recency weights: the newest session weighs WEIGHT_SPREAD, the next WEIGHT_SPREAD-1, and so
// on down to 1. Sessions beyond HISTORY_DEPTH are not kept.
constexpr int HISTORY_DEPTH = 5;
constexpr int WEIGHT_SPREAD = 10;

constexpr int SCHEMA_VERSION = 1;

// Above this size the file is rebuilt after a store, since freed pages are never returned
// otherwise.
constexpr qint64 VACUUM_THRESHOLD_BYTES = 10 * 1024 * 1024;

// Schema text is identical to the legacy app's, so both can open the same cache.db.
constexpr QLatin1StringView SCHEMA{R"sql(
    CREATE TABLE IF NOT EXISTS startup_impact_sessions (
        id              INTEGER PRIMARY KEY AUTOINCREMENT,
        recorded_at     REAL NOT NULL,
        loading_time_s  REAL NOT NULL DEFAULT 0,
        timestamp       TEXT NOT NULL DEFAULT ''
    );

    CREATE TABLE IF NOT EXISTS startup_impact_entries (
        id                      INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id              INTEGER NOT NULL,
        package_id              TEXT NOT NULL,
        mod_name                TEXT NOT NULL DEFAULT '',
        total_impact_s          REAL NOT NULL DEFAULT 0,
        metrics_json            TEXT NOT NULL DEFAULT '{}',
        off_thread_metrics_json TEXT NOT NULL DEFAULT '{}',
        off_thread_total_s      REAL NOT NULL DEFAULT 0,
        FOREIGN KEY (session_id)
            REFERENCES startup_impact_sessions(id) ON DELETE CASCADE
    );

    CREATE INDEX IF NOT EXISTS idx_sie_session
        ON startup_impact_entries(session_id);
    CREATE INDEX IF NOT EXISTS idx_sie_pid
        ON startup_impact_entries(package_id);
)sql"};

// Ranks the newest HISTORY_DEPTH sessions and assigns each a recency weight.
// Binds, in order: HISTORY_DEPTH, WEIGHT_SPREAD. Exposes columns session_id and weight.
constexpr QLatin1StringView WEIGHTED_SESSIONS_CTE{R"sql(
    ranked AS (
        SELECT id AS session_id,
               ROW_NUMBER() OVER (ORDER BY id DESC) AS rn
        FROM startup_impact_sessions
        ORDER BY id DESC
        LIMIT ?
    ),
    w AS (
        SELECT session_id,
               MAX(? - rn + 1, 1) AS weight
        FROM ranked
    )
)sql"};

// Wraps a SELECT that joins the weighted sessions as `w`.
QString with_weights(const QString& select_sql) {
    return QStringLiteral("WITH ") + QString(WEIGHTED_SESSIONS_CTE) + select_sql;
}

// The CTE's two parameters come first, followed by the query's own.
QVariantList with_cte_binds(const QVariantList& rest = {}) {
    QVariantList binds{HISTORY_DEPTH, WEIGHT_SPREAD};
    binds.append(rest);
    return binds;
}

QVariantList to_variants(const QStringList& values) {
    QVariantList binds;
    binds.reserve(values.size());
    for (const QString& value : values) {
        binds.append(value);
    }
    return binds;
}

QString placeholder_list(qsizetype count) {
    QStringList marks(count, QStringLiteral("?"));
    return marks.join(QLatin1Char(','));
}

// Prepares and runs one statement. The returned query stays positioned on the first row.
Result<QSqlQuery> exec(QSqlDatabase& db, const QString& sql, const QVariantList& binds = {}) {
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        return std::unexpected(Error{query.lastError().text()});
    }
    for (const QVariant& value : binds) {
        query.addBindValue(value);
    }
    if (!query.exec()) {
        return std::unexpected(Error{query.lastError().text()});
    }
    return query;
}

Result<void> run(QSqlDatabase& db, const QString& sql, const QVariantList& binds = {}) {
    auto executed = exec(db, sql, binds);
    if (!executed) {
        return std::unexpected(executed.error());
    }
    return {};
}

// Stored metric maps hold seconds already. Non-numeric values are skipped.
Result<QMap<QString, double>> load_metric_map(const QString& json_text) {
    const QByteArray bytes = json_text.isEmpty() ? QByteArrayLiteral("{}") : json_text.toUtf8();
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(Error{QStringLiteral("Invalid stored metrics: %1")
                                         .arg(error.errorString())});
    }
    QMap<QString, double> values;
    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.value().isDouble()) {
            values.insert(it.key(), it.value().toDouble());
        }
    }
    return values;
}

QString metrics_to_json(const QMap<QString, double>& metrics) {
    QJsonObject object;
    for (auto it = metrics.cbegin(); it != metrics.cend(); ++it) {
        object.insert(it.key(), it.value());
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

Result<StartupImpactMod> row_to_mod(const QSqlQuery& row) {
    const QString package_id = row.value(0).toString();
    const QString mod_name = row.value(1).toString();
    // A report without a package id stores the mod name in its place. Read it back as absent.
    std::optional<QString> pid = package_id;
    if (package_id == mod_name && package_id != BASE_GAME_PID) {
        pid = std::nullopt;
    }

    auto metrics = load_metric_map(row.value(3).toString());
    if (!metrics) {
        return std::unexpected(metrics.error());
    }
    auto off_thread = load_metric_map(row.value(4).toString());
    if (!off_thread) {
        return std::unexpected(off_thread.error());
    }

    return StartupImpactMod{
        .mod_name = mod_name,
        .package_id = pid,
        .total_impact_s = row.value(2).toDouble(),
        .metrics = std::move(*metrics),
        .off_thread_metrics = std::move(*off_thread),
        .off_thread_total_impact_s = row.value(5).toDouble(),
    };
}

Result<QList<StartupImpactMod>> load_mods(QSqlDatabase& db, qlonglong session_id) {
    auto query = exec(db,
                      "SELECT package_id, mod_name, total_impact_s,"
                      " metrics_json, off_thread_metrics_json, off_thread_total_s"
                      " FROM startup_impact_entries WHERE session_id = ?",
                      {session_id});
    if (!query) {
        return std::unexpected(query.error());
    }
    QList<StartupImpactMod> mods;
    while (query->next()) {
        auto mod = row_to_mod(*query);
        if (!mod) {
            return std::unexpected(mod.error());
        }
        mods.append(std::move(*mod));
    }
    return mods;
}

void close_connection(StartupImpactDb::Connection& conn) {
    if (!conn.open) {
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(conn.name, false);
        db.close();
    }
    // removeDatabase requires that no QSqlDatabase handle for the connection remains.
    QSqlDatabase::removeDatabase(conn.name);
    conn.open = false;
    conn.path.clear();
}

// Opens the connection on the worker, applying the legacy pragmas and schema. Reuses the open
// connection when the path has not changed.
Result<QSqlDatabase> open_connection(StartupImpactDb::Connection& conn, const QString& path) {
    if (conn.open && conn.path == path) {
        return QSqlDatabase::database(conn.name, false);
    }
    close_connection(conn);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn.name);
    db.setDatabaseName(path);

    const auto prepare = [&db, &path]() -> Result<void> {
        if (!db.open()) {
            return std::unexpected(Error{db.lastError().text()});
        }
        // Order matters: auto_vacuum only takes effect before the first table is created.
        for (const char* pragma : {"PRAGMA journal_mode=WAL", "PRAGMA busy_timeout=5000",
                                   "PRAGMA foreign_keys=ON", "PRAGMA auto_vacuum=INCREMENTAL"}) {
            if (auto applied = run(db, QString::fromLatin1(pragma)); !applied) {
                return applied;
            }
        }
        return ensure_schema(db, QString(SCHEMA), SCHEMA_VERSION, {}, path,
                             QStringLiteral("DB schema"));
    };

    if (auto ready = prepare(); !ready) {
        const Error error = ready.error();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(conn.name);
        return std::unexpected(error);
    }
    conn.path = path;
    conn.open = true;
    return db;
}

// Read paths return empty results for a missing file rather than creating one.
bool file_exists(const QString& path) { return QFileInfo::exists(path); }

Result<void> ensure_parent_dir(const QString& path) {
    if (file_exists(path)) {
        return {};
    }
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) {
        return std::unexpected(Error{QStringLiteral("Cannot create directory %1").arg(dir)});
    }
    return {};
}

Result<void> insert_report(QSqlDatabase& db, const StartupImpactReport& report) {
    const QString timestamp = report.timestamp.isNull() ? QStringLiteral("") : report.timestamp;
    auto session = exec(db,
                        "INSERT INTO startup_impact_sessions"
                        " (recorded_at, loading_time_s, timestamp) VALUES (?, ?, ?)",
                        {QDateTime::currentMSecsSinceEpoch() / 1000.0, report.loading_time_s,
                         timestamp});
    if (!session) {
        return std::unexpected(session.error());
    }
    const qlonglong session_id = session->lastInsertId().toLongLong();

    for (const StartupImpactMod& mod : report.mods) {
        const bool has_package_id = mod.package_id && !mod.package_id->isEmpty();
        const QString name = mod.mod_name.isNull() ? QStringLiteral("") : mod.mod_name;
        const QString pid = has_package_id ? *mod.package_id : name;
        auto entry = run(db,
                         "INSERT INTO startup_impact_entries"
                         " (session_id, package_id, mod_name, total_impact_s,"
                         "  metrics_json, off_thread_metrics_json, off_thread_total_s)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?)",
                         {session_id, pid, name, mod.total_impact_s,
                          metrics_to_json(mod.metrics), metrics_to_json(mod.off_thread_metrics),
                          mod.off_thread_total_impact_s});
        if (!entry) {
            return entry;
        }
    }

    return run(db,
               "DELETE FROM startup_impact_sessions WHERE id NOT IN"
               " (SELECT id FROM startup_impact_sessions ORDER BY id DESC LIMIT ?)",
               {HISTORY_DEPTH});
}

Result<void> compact(QSqlDatabase& db, const QString& path) {
    // Checkpoint the WAL, then reclaim free pages. The file is only rebuilt when it has grown.
    if (auto checkpointed = run(db, "PRAGMA wal_checkpoint(TRUNCATE)"); !checkpointed) {
        return checkpointed;
    }
    if (auto vacuumed = run(db, "PRAGMA incremental_vacuum"); !vacuumed) {
        return vacuumed;
    }
    if (QFileInfo(path).size() > VACUUM_THRESHOLD_BYTES) {
        return run(db, "VACUUM");
    }
    return {};
}

// Runs `work` inside one transaction, rolling back when it fails.
template <class Work>
Result<void> in_transaction(QSqlDatabase& db, Work&& work) {
    if (!db.transaction()) {
        return std::unexpected(Error{db.lastError().text()});
    }
    if (auto done = work(); !done) {
        db.rollback();
        return done;
    }
    if (!db.commit()) {
        const Error error{db.lastError().text()};
        db.rollback();
        return std::unexpected(error);
    }
    return {};
}

Result<void> store_report_sync(StartupImpactDb::Connection& conn, const QString& path,
                               const StartupImpactReport& report) {
    if (auto dir = ensure_parent_dir(path); !dir) {
        return dir;
    }
    auto opened = open_connection(conn, path);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    QSqlDatabase db = *opened;
    if (auto stored = in_transaction(db, [&] { return insert_report(db, report); }); !stored) {
        return stored;
    }
    return compact(db, path);
}

Result<std::optional<StartupImpactReport>> get_latest_report_sync(StartupImpactDb::Connection& conn,
                                                                  const QString& path) {
    if (!file_exists(path)) {
        return std::nullopt;
    }
    auto opened = open_connection(conn, path);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    QSqlDatabase db = *opened;

    auto session = exec(db,
                        "SELECT id, loading_time_s, timestamp"
                        " FROM startup_impact_sessions ORDER BY id DESC LIMIT 1");
    if (!session) {
        return std::unexpected(session.error());
    }
    if (!session->next()) {
        return std::nullopt;
    }
    const qlonglong session_id = session->value(0).toLongLong();
    StartupImpactReport report;
    report.loading_time_s = session->value(1).toDouble();
    report.timestamp = session->value(2).toString();

    auto mods = load_mods(db, session_id);
    if (!mods) {
        return std::unexpected(mods.error());
    }
    report.mods = std::move(*mods);
    return report;
}

Result<double> average_for(QSqlDatabase& db, const QString& package_id) {
    auto query = exec(db,
                      with_weights("SELECT COALESCE(SUM(e.total_impact_s * w.weight) * 1.0"
                                   " / NULLIF(SUM(w.weight), 0), 0)"
                                   " FROM startup_impact_entries e"
                                   " JOIN w ON w.session_id = e.session_id"
                                   " WHERE e.package_id = ?"),
                      with_cte_binds({package_id}));
    if (!query) {
        return std::unexpected(query.error());
    }
    return query->next() ? query->value(0).toDouble() : 0.0;
}

Result<QHash<QString, ThreadAverages>> totals_for(QSqlDatabase& db, const QStringList& package_ids) {
    auto query = exec(db,
                      with_weights("SELECT e.package_id,"
                                   " COALESCE(SUM(e.total_impact_s * w.weight)"
                                   "     * 1.0 / NULLIF(SUM(w.weight), 0), 0),"
                                   " COALESCE(SUM(e.off_thread_total_s * w.weight)"
                                   "     * 1.0 / NULLIF(SUM(w.weight), 0), 0)"
                                   " FROM startup_impact_entries e"
                                   " JOIN w ON w.session_id = e.session_id"
                                   " WHERE e.package_id IN (" +
                                   placeholder_list(package_ids.size()) +
                                   ") GROUP BY e.package_id"),
                      with_cte_binds(to_variants(package_ids)));
    if (!query) {
        return std::unexpected(query.error());
    }
    QHash<QString, ThreadAverages> totals;
    while (query->next()) {
        totals.insert(query->value(0).toString(),
                      ThreadAverages{query->value(1).toDouble(), query->value(2).toDouble()});
    }
    return totals;
}

Result<QHash<QString, double>> all_averages_for(QSqlDatabase& db) {
    auto query = exec(db,
                      with_weights("SELECT e.package_id,"
                                   " COALESCE(SUM(e.total_impact_s * w.weight)"
                                   "     * 1.0 / NULLIF(SUM(w.weight), 0), 0)"
                                   " FROM startup_impact_entries e"
                                   " JOIN w ON w.session_id = e.session_id"
                                   " GROUP BY e.package_id"),
                      with_cte_binds());
    if (!query) {
        return std::unexpected(query.error());
    }
    QHash<QString, double> averages;
    while (query->next()) {
        averages.insert(query->value(0).toString(), query->value(1).toDouble());
    }
    return averages;
}

Result<void> clear_sync(StartupImpactDb::Connection& conn, const QString& path) {
    if (!file_exists(path)) {
        return {};
    }
    auto opened = open_connection(conn, path);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    QSqlDatabase db = *opened;
    if (auto cleared = in_transaction(db, [&]() -> Result<void> {
            if (auto entries = run(db, "DELETE FROM startup_impact_entries"); !entries) {
                return entries;
            }
            return run(db, "DELETE FROM startup_impact_sessions");
        });
        !cleared) {
        return cleared;
    }
    if (auto vacuumed = run(db, "VACUUM"); !vacuumed) {
        return vacuumed;
    }
    return run(db, "PRAGMA wal_checkpoint(TRUNCATE)");
}

}  // namespace

QString db_path(const QString& config_dir) {
    return QDir(config_dir).filePath(QStringLiteral("cache.db"));
}

StartupImpactDb::StartupImpactDb() {
    // A single thread keeps the connection on one thread. Idle threads must not expire, since
    // the connection is bound to the thread that opened it.
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(-1);
    conn_.name = QStringLiteral("startup_impact_%1")
                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

StartupImpactDb::~StartupImpactDb() { close_sync(); }

QCoro::Task<Result<void>> StartupImpactDb::store_report(QString path, StartupImpactReport report) {
    co_return co_await QtConcurrent::run(&pool_, [this, path = std::move(path),
                                                  report = std::move(report)] {
        return store_report_sync(conn_, path, report);
    });
}

QCoro::Task<Result<std::optional<StartupImpactReport>>> StartupImpactDb::get_latest_report(
    QString path) {
    co_return co_await QtConcurrent::run(&pool_, [this, path = std::move(path)] {
        return get_latest_report_sync(conn_, path);
    });
}

QCoro::Task<Result<double>> StartupImpactDb::get_average(QString path, QString package_id) {
    co_return co_await QtConcurrent::run(
        &pool_, [this, path = std::move(path), package_id = std::move(package_id)]() -> Result<double> {
            if (!file_exists(path)) {
                return 0.0;
            }
            auto opened = open_connection(conn_, path);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            QSqlDatabase db = *opened;
            return average_for(db, package_id);
        });
}

QCoro::Task<Result<QHash<QString, ThreadAverages>>> StartupImpactDb::get_totals_for(
    QString path, QStringList package_ids) {
    co_return co_await QtConcurrent::run(
        &pool_, [this, path = std::move(path), package_ids = std::move(package_ids)]()
                    -> Result<QHash<QString, ThreadAverages>> {
            // Ids without history are left out, so callers treat them as zero.
            if (package_ids.isEmpty() || !file_exists(path)) {
                return QHash<QString, ThreadAverages>{};
            }
            auto opened = open_connection(conn_, path);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            QSqlDatabase db = *opened;
            return totals_for(db, package_ids);
        });
}

QCoro::Task<Result<QHash<QString, double>>> StartupImpactDb::get_all_averages(QString path) {
    co_return co_await QtConcurrent::run(
        &pool_, [this, path = std::move(path)]() -> Result<QHash<QString, double>> {
            if (!file_exists(path)) {
                return QHash<QString, double>{};
            }
            auto opened = open_connection(conn_, path);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            QSqlDatabase db = *opened;
            return all_averages_for(db);
        });
}

QCoro::Task<Result<LatestWithAverages>> StartupImpactDb::get_latest_with_averages(
    QString path, QStringList active_pids, std::optional<QString> selected_pid) {
    co_return co_await QtConcurrent::run(
        &pool_, [this, path = std::move(path), active_pids = std::move(active_pids),
                 selected_pid = std::move(selected_pid)]() -> Result<LatestWithAverages> {
            LatestWithAverages result;
            for (const QString& pid : active_pids) {
                result.totals.insert(pid, ThreadAverages{});
            }
            if (!file_exists(path)) {
                return result;
            }

            auto latest = get_latest_report_sync(conn_, path);
            if (!latest) {
                return std::unexpected(latest.error());
            }
            result.report = std::move(*latest);

            auto opened = open_connection(conn_, path);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            QSqlDatabase db = *opened;

            auto base = average_for(db, QString(BASE_GAME_PID));
            if (!base) {
                return std::unexpected(base.error());
            }
            result.base_game_avg = *base;

            if (!active_pids.isEmpty()) {
                auto totals = totals_for(db, active_pids);
                if (!totals) {
                    return std::unexpected(totals.error());
                }
                for (auto it = totals->cbegin(); it != totals->cend(); ++it) {
                    result.totals.insert(it.key(), it.value());
                }
            }

            if (selected_pid) {
                auto selected = average_for(db, *selected_pid);
                if (!selected) {
                    return std::unexpected(selected.error());
                }
                result.selected_avg = *selected;
            }
            return result;
        });
}

QCoro::Task<Result<void>> StartupImpactDb::clear(QString path) {
    co_return co_await QtConcurrent::run(&pool_, [this, path = std::move(path)] {
        return clear_sync(conn_, path);
    });
}

QCoro::Task<void> StartupImpactDb::close() {
    co_await QtConcurrent::run(&pool_, [this] { close_connection(conn_); });
}

void StartupImpactDb::close_sync() {
    QtConcurrent::run(&pool_, [this] { close_connection(conn_); }).waitForFinished();
}

}  // namespace px::modrim::startup_impact
