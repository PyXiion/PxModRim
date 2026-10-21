#include "core/services/startup_impact_service/db.hpp"

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
#include <initializer_list>

#include <QCoro/QCoroFuture>
#include <QtConcurrent/QtConcurrentRun>

#include "core/migrator.hpp"

namespace px::modrim::startup_impact {

namespace {

// Recency weights: the newest session weighs WEIGHT_SPREAD, the next
// WEIGHT_SPREAD-1, and so on down to 1. Sessions beyond HISTORY_DEPTH are not
// kept.
constexpr int kHistoryDepth = 5;
constexpr int kWeightSpread = 10;

constexpr int kSchemaVersion = 1;

// Above this size the file is rebuilt after a store, since freed pages are
// never returned otherwise.
constexpr qint64 kVacuumThresholdBytes = 10 * 1024 * 1024;

// Schema text is identical to the legacy app's, so both can open the same
// cache.db.
constexpr QLatin1StringView kSchema{R"sql(
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
// Binds, in order: HISTORY_DEPTH, WEIGHT_SPREAD. Exposes columns session_id and
// weight.
constexpr QLatin1StringView kWeightedSessionsCte{R"sql(
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
QString withWeights(const QString& selectSql) {
  return QStringLiteral("WITH ") + QString(kWeightedSessionsCte) + selectSql;
}

// The CTE's two parameters come first, followed by the query's own.
QVariantList withCteBinds(const QVariantList& rest = {}) {
  QVariantList binds{kHistoryDepth, kWeightSpread};
  binds.append(rest);
  return binds;
}

QVariantList toVariants(const QStringList& values) {
  QVariantList binds;
  binds.reserve(values.size());
  for (const QString& value : values) {
    binds.append(value);
  }
  return binds;
}

QString placeholderList(qsizetype count) {
  QStringList marks(count, QStringLiteral("?"));
  return marks.join(QLatin1Char(','));
}

// Prepares and runs one statement. The returned query stays positioned on the
// first row.
Result<QSqlQuery> exec(QSqlDatabase& db, const QString& sql,
                       const QVariantList& binds = {}) {
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

Result<void> run(QSqlDatabase& db, const QString& sql,
                 const QVariantList& binds = {}) {
  auto executed = exec(db, sql, binds);
  if (!executed) {
    return std::unexpected(executed.error());
  }
  return {};
}

// Stored metric maps hold seconds already. Non-numeric values are skipped.
Result<QMap<QString, double>> loadMetricMap(const QString& jsonText) {
  const QByteArray bytes =
      jsonText.isEmpty() ? QByteArrayLiteral("{}") : jsonText.toUtf8();
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return std::unexpected(Error{
        QStringLiteral("Invalid stored metrics: %1").arg(error.errorString())});
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

QString metricsToJson(const QMap<QString, double>& metrics) {
  QJsonObject object;
  for (auto it = metrics.cbegin(); it != metrics.cend(); ++it) {
    object.insert(it.key(), it.value());
  }
  return QString::fromUtf8(
      QJsonDocument(object).toJson(QJsonDocument::Compact));
}

Result<StartupImpactMod> rowToMod(const QSqlQuery& row) {
  const QString packageId = row.value(0).toString();
  const QString modName = row.value(1).toString();
  // A report without a package id stores the mod name in its place. Read it
  // back as absent.
  std::optional<QString> pid = packageId;
  if (packageId == modName && packageId != kBaseGamePid) {
    pid = std::nullopt;
  }

  auto metrics = loadMetricMap(row.value(3).toString());
  if (!metrics) {
    return std::unexpected(metrics.error());
  }
  auto offThread = loadMetricMap(row.value(4).toString());
  if (!offThread) {
    return std::unexpected(offThread.error());
  }

  return StartupImpactMod{
      .modName = modName,
      .packageId = pid,
      .totalImpactS = row.value(2).toDouble(),
      .metrics = std::move(*metrics),
      .offThreadMetrics = std::move(*offThread),
      .offThreadTotalImpactS = row.value(5).toDouble(),
  };
}

Result<QList<StartupImpactMod>> loadMods(QSqlDatabase& db,
                                         qlonglong sessionId) {
  auto query = exec(db,
                    "SELECT package_id, mod_name, total_impact_s,"
                    " metrics_json, off_thread_metrics_json, off_thread_total_s"
                    " FROM startup_impact_entries WHERE session_id = ?",
                    {sessionId});
  if (!query) {
    return std::unexpected(query.error());
  }
  QList<StartupImpactMod> mods;
  while (query->next()) {
    auto mod = rowToMod(*query);
    if (!mod) {
      return std::unexpected(mod.error());
    }
    mods.append(std::move(*mod));
  }
  return mods;
}

void closeConnection(StartupImpactDb::Connection& conn) {
  if (!conn.open) {
    return;
  }
  {
    QSqlDatabase db = QSqlDatabase::database(conn.name, false);
    db.close();
  }
  // removeDatabase requires that no QSqlDatabase handle for the connection
  // remains.
  QSqlDatabase::removeDatabase(conn.name);
  conn.open = false;
  conn.path.clear();
}

// Opens the connection on the worker, applying the legacy pragmas and schema.
// Reuses the open connection when the path has not changed.
Result<QSqlDatabase> openConnection(StartupImpactDb::Connection& conn,
                                    const QString& path) {
  if (conn.open && conn.path == path) {
    return QSqlDatabase::database(conn.name, false);
  }
  closeConnection(conn);

  QSqlDatabase db =
      QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn.name);
  db.setDatabaseName(path);

  const auto prepare = [&db, &path]() -> Result<void> {
    if (!db.open()) {
      return std::unexpected(Error{db.lastError().text()});
    }
    // Order matters: auto_vacuum only takes effect before the first table is
    // created.
    for (const char* pragma :
         {"PRAGMA journal_mode=WAL", "PRAGMA busy_timeout=5000",
          "PRAGMA foreign_keys=ON", "PRAGMA auto_vacuum=INCREMENTAL"}) {
      if (auto applied = run(db, QString::fromLatin1(pragma)); !applied) {
        return applied;
      }
    }
    return ensureSchema(db, QString(kSchema), kSchemaVersion, {}, path,
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
bool fileExists(const QString& path) { return QFileInfo::exists(path); }

template <class T, class Fn>
Result<T> withExistingDb(StartupImpactDb::Connection& conn, const QString& path,
                         T empty, Fn&& fn) {
  if (!fileExists(path)) {
    return empty;
  }
  auto opened = openConnection(conn, path);
  if (!opened) {
    return std::unexpected(opened.error());
  }
  QSqlDatabase db = *opened;
  return fn(db);
}

Result<void> ensureParentDir(const QString& path) {
  if (fileExists(path)) {
    return {};
  }
  const QString dir = QFileInfo(path).absolutePath();
  if (!QDir().mkpath(dir)) {
    return std::unexpected(
        Error{QStringLiteral("Cannot create directory %1").arg(dir)});
  }
  return {};
}

Result<void> insertReport(QSqlDatabase& db, const StartupImpactReport& report) {
  const QString timestamp =
      report.timestamp.isNull() ? QStringLiteral("") : report.timestamp;
  auto session =
      exec(db,
           "INSERT INTO startup_impact_sessions"
           " (recorded_at, loading_time_s, timestamp) VALUES (?, ?, ?)",
           {QDateTime::currentMSecsSinceEpoch() / 1000.0, report.loadingTimeS,
            timestamp});
  if (!session) {
    return std::unexpected(session.error());
  }
  const qlonglong sessionId = session->lastInsertId().toLongLong();

  for (const StartupImpactMod& mod : report.mods) {
    const bool hasPackageId = mod.packageId && !mod.packageId->isEmpty();
    const QString name =
        mod.modName.isNull() ? QStringLiteral("") : mod.modName;
    const QString pid = hasPackageId ? *mod.packageId : name;
    auto entry =
        run(db,
            "INSERT INTO startup_impact_entries"
            " (session_id, package_id, mod_name, total_impact_s,"
            "  metrics_json, off_thread_metrics_json, off_thread_total_s)"
            " VALUES (?, ?, ?, ?, ?, ?, ?)",
            {sessionId, pid, name, mod.totalImpactS, metricsToJson(mod.metrics),
             metricsToJson(mod.offThreadMetrics), mod.offThreadTotalImpactS});
    if (!entry) {
      return entry;
    }
  }

  return run(
      db,
      "DELETE FROM startup_impact_sessions WHERE id NOT IN"
      " (SELECT id FROM startup_impact_sessions ORDER BY id DESC LIMIT ?)",
      {kHistoryDepth});
}

Result<void> compact(QSqlDatabase& db, const QString& path) {
  // Checkpoint the WAL, then reclaim free pages. The file is only rebuilt when
  // it has grown.
  if (auto checkpointed = run(db, "PRAGMA wal_checkpoint(TRUNCATE)");
      !checkpointed) {
    return checkpointed;
  }
  if (auto vacuumed = run(db, "PRAGMA incremental_vacuum"); !vacuumed) {
    return vacuumed;
  }
  if (QFileInfo(path).size() > kVacuumThresholdBytes) {
    return run(db, "VACUUM");
  }
  return {};
}

// Runs `work` inside one transaction, rolling back when it fails.
template <class Work>
Result<void> inTransaction(QSqlDatabase& db, Work&& work) {
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

Result<void> storeReportSync(StartupImpactDb::Connection& conn,
                             const QString& path,
                             const StartupImpactReport& report) {
  if (auto dir = ensureParentDir(path); !dir) {
    return dir;
  }
  auto opened = openConnection(conn, path);
  if (!opened) {
    return std::unexpected(opened.error());
  }
  QSqlDatabase db = *opened;
  if (auto stored = inTransaction(db, [&] { return insertReport(db, report); });
      !stored) {
    return stored;
  }
  return compact(db, path);
}

Result<std::optional<StartupImpactReport>> latestReportFrom(QSqlDatabase& db) {
  auto session = exec(db,
                      "SELECT id, loading_time_s, timestamp"
                      " FROM startup_impact_sessions ORDER BY id DESC LIMIT 1");
  if (!session) {
    return std::unexpected(session.error());
  }
  if (!session->next()) {
    return std::nullopt;
  }
  const qlonglong sessionId = session->value(0).toLongLong();
  StartupImpactReport report;
  report.loadingTimeS = session->value(1).toDouble();
  report.timestamp = session->value(2).toString();

  auto mods = loadMods(db, sessionId);
  if (!mods) {
    return std::unexpected(mods.error());
  }
  report.mods = std::move(*mods);
  return report;
}

Result<double> averageFor(QSqlDatabase& db, const QString& packageId) {
  auto query =
      exec(db,
           withWeights("SELECT COALESCE(SUM(e.total_impact_s * w.weight) * 1.0"
                       " / NULLIF(SUM(w.weight), 0), 0)"
                       " FROM startup_impact_entries e"
                       " JOIN w ON w.session_id = e.session_id"
                       " WHERE e.package_id = ?"),
           withCteBinds({packageId}));
  if (!query) {
    return std::unexpected(query.error());
  }
  return query->next() ? query->value(0).toDouble() : 0.0;
}

Result<QHash<QString, ThreadAverages>> totalsFor(
    QSqlDatabase& db, const QStringList& packageIds) {
  auto query = exec(db,
                    withWeights("SELECT e.package_id,"
                                " COALESCE(SUM(e.total_impact_s * w.weight)"
                                "     * 1.0 / NULLIF(SUM(w.weight), 0), 0),"
                                " COALESCE(SUM(e.off_thread_total_s * w.weight)"
                                "     * 1.0 / NULLIF(SUM(w.weight), 0), 0)"
                                " FROM startup_impact_entries e"
                                " JOIN w ON w.session_id = e.session_id"
                                " WHERE e.package_id IN (" +
                                placeholderList(packageIds.size()) +
                                ") GROUP BY e.package_id"),
                    withCteBinds(toVariants(packageIds)));
  if (!query) {
    return std::unexpected(query.error());
  }
  QHash<QString, ThreadAverages> totals;
  while (query->next()) {
    totals.insert(
        query->value(0).toString(),
        ThreadAverages{query->value(1).toDouble(), query->value(2).toDouble()});
  }
  return totals;
}

Result<QHash<QString, double>> allAveragesFor(QSqlDatabase& db) {
  auto query = exec(db,
                    withWeights("SELECT e.package_id,"
                                " COALESCE(SUM(e.total_impact_s * w.weight)"
                                "     * 1.0 / NULLIF(SUM(w.weight), 0), 0)"
                                " FROM startup_impact_entries e"
                                " JOIN w ON w.session_id = e.session_id"
                                " GROUP BY e.package_id"),
                    withCteBinds());
  if (!query) {
    return std::unexpected(query.error());
  }
  QHash<QString, double> averages;
  while (query->next()) {
    averages.insert(query->value(0).toString(), query->value(1).toDouble());
  }
  return averages;
}

Result<void> clearSync(StartupImpactDb::Connection& conn, const QString& path) {
  if (!fileExists(path)) {
    return {};
  }
  auto opened = openConnection(conn, path);
  if (!opened) {
    return std::unexpected(opened.error());
  }
  QSqlDatabase db = *opened;
  if (auto cleared = inTransaction(
          db,
          [&]() -> Result<void> {
            if (auto entries = run(db, "DELETE FROM startup_impact_entries");
                !entries) {
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

QString dbPath(const QString& configDir) {
  return QDir(configDir).filePath(QStringLiteral("cache.db"));
}

StartupImpactDb::StartupImpactDb() {
  // A single thread keeps the connection on one thread. Idle threads must not
  // expire, since the connection is bound to the thread that opened it.
  m_pool.setMaxThreadCount(1);
  m_pool.setExpiryTimeout(-1);
  m_conn.name = QStringLiteral("startup_impact_%1")
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

StartupImpactDb::~StartupImpactDb() { closeSync(); }

QCoro::Task<Result<void>> StartupImpactDb::storeReport(
    QString path, StartupImpactReport report) {
  co_return co_await QtConcurrent::run(
      &m_pool, [this, path = std::move(path), report = std::move(report)] {
        return storeReportSync(m_conn, path, report);
      });
}

QCoro::Task<Result<std::optional<StartupImpactReport>>>
StartupImpactDb::getLatestReport(QString path) {
  co_return co_await QtConcurrent::run(&m_pool, [this, path = std::move(path)] {
    return withExistingDb<std::optional<StartupImpactReport>>(
        m_conn, path, std::nullopt,
        [](QSqlDatabase& db) { return latestReportFrom(db); });
  });
}

QCoro::Task<Result<QHash<QString, double>>> StartupImpactDb::getAllAverages(
    QString path) {
  co_return co_await QtConcurrent::run(
      &m_pool,
      [this, path = std::move(path)]() -> Result<QHash<QString, double>> {
        return withExistingDb<QHash<QString, double>>(
            m_conn, path, {},
            [](QSqlDatabase& db) { return allAveragesFor(db); });
      });
}

QCoro::Task<Result<LatestWithAverages>> StartupImpactDb::getLatestWithAverages(
    QString path, QStringList activePids, std::optional<QString> selectedPid) {
  co_return co_await QtConcurrent::run(
      &m_pool,
      [this, path = std::move(path), activePids = std::move(activePids),
       selectedPid = std::move(selectedPid)]() -> Result<LatestWithAverages> {
        LatestWithAverages result;
        for (const QString& pid : activePids) {
          result.totals.insert(pid, ThreadAverages{});
        }
        if (!fileExists(path)) {
          return result;
        }

        auto opened = openConnection(m_conn, path);
        if (!opened) {
          return std::unexpected(opened.error());
        }
        QSqlDatabase db = *opened;

        auto latest = latestReportFrom(db);
        if (!latest) {
          return std::unexpected(latest.error());
        }
        result.report = std::move(*latest);

        auto base = averageFor(db, QString(kBaseGamePid));
        if (!base) {
          return std::unexpected(base.error());
        }
        result.baseGameAvg = *base;

        if (!activePids.isEmpty()) {
          auto totals = totalsFor(db, activePids);
          if (!totals) {
            return std::unexpected(totals.error());
          }
          for (auto it = totals->cbegin(); it != totals->cend(); ++it) {
            result.totals.insert(it.key(), it.value());
          }
        }

        if (selectedPid) {
          auto selected = averageFor(db, *selectedPid);
          if (!selected) {
            return std::unexpected(selected.error());
          }
          result.selectedAvg = *selected;
        }
        return result;
      });
}

QCoro::Task<Result<void>> StartupImpactDb::clear(QString path) {
  co_return co_await QtConcurrent::run(&m_pool, [this, path = std::move(path)] {
    return clearSync(m_conn, path);
  });
}

QCoro::Task<void> StartupImpactDb::close() {
  co_await QtConcurrent::run(&m_pool, [this] { closeConnection(m_conn); });
}

void StartupImpactDb::closeSync() {
  QtConcurrent::run(&m_pool, [this] {
    closeConnection(m_conn);
  }).waitForFinished();
}

}  // namespace px::modrim::startup_impact
