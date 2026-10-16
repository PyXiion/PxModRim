#include "core/organizer/db.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <type_traits>
#include <utility>

#include <QCoro/QCoroFuture>
#include <QtConcurrent/QtConcurrentRun>

#include "core/migrator.hpp"

namespace px::modrim::organizer {

namespace {

// Byte-for-byte the legacy schema: existing user databases carry this exact
// version-1 layout.
const QString kSchema = QStringLiteral(R"sql(
CREATE TABLE IF NOT EXISTS folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    collapsed INTEGER NOT NULL DEFAULT 0,
    CHECK ((id = 1) = (parent_id IS NULL))
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_folders_parent_name
    ON folders (parent_id, name COLLATE NOCASE);

CREATE TABLE IF NOT EXISTS placements (
    package_id TEXT PRIMARY KEY COLLATE NOCASE,
    folder_id INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE COLLATE NOCASE,
    color TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS mod_tags (
    package_id TEXT NOT NULL COLLATE NOCASE,
    tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY (package_id, tag_id)
);

CREATE TABLE IF NOT EXISTS rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    position INTEGER NOT NULL,
    field TEXT NOT NULL CHECK (field IN ('package_id', 'name', 'author')),
    op TEXT NOT NULL CHECK (op IN ('prefix', 'contains', 'equals')),
    pattern TEXT NOT NULL,
    folder_id INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE
);

INSERT OR IGNORE INTO folders (id, parent_id, name, collapsed)
VALUES (1, NULL, 'Root', 0);
)sql");

constexpr int kSchemaVersion = 1;

const QString kPlaceSql = QStringLiteral(
    "INSERT INTO placements (package_id, folder_id) VALUES (?, ?) "
    "ON CONFLICT (package_id) DO UPDATE SET folder_id = excluded.folder_id");
const QString kInsertRuleSql = QStringLiteral(
    "INSERT INTO rules (position, field, op, pattern, folder_id) VALUES (?, ?, "
    "?, ?, ?)");
// Re-parents placements of the whole subtree to the root before the folders are
// deleted.
const QString kMoveSubtreeSql = QStringLiteral(
    "WITH RECURSIVE subtree(id) AS ("
    "    SELECT ?"
    "    UNION ALL"
    "    SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id"
    ") "
    "UPDATE placements SET folder_id = ? WHERE folder_id IN (SELECT id FROM "
    "subtree)");

Error errorOf(const QSqlQuery& query) {
  return Error{query.lastError().text()};
}

Result<QSqlQuery> runStatement(QSqlDatabase& db, const QString& sql,
                               const QVariantList& params = {}) {
  QSqlQuery query(db);
  if (!query.prepare(sql)) {
    return std::unexpected(errorOf(query));
  }
  for (qsizetype i = 0; i < params.size(); ++i) {
    query.bindValue(static_cast<int>(i), params[i]);
  }
  if (!query.exec()) {
    return std::unexpected(errorOf(query));
  }
  return query;
}

// Prepares `sql` once and runs it for each row. Positional values are rebound
// every time.
Result<void> runBatch(QSqlDatabase& db, const QString& sql,
                      const QList<QVariantList>& rows) {
  QSqlQuery query(db);
  if (!query.prepare(sql)) {
    return std::unexpected(errorOf(query));
  }
  for (const QVariantList& row : rows) {
    for (qsizetype i = 0; i < row.size(); ++i) {
      query.bindValue(static_cast<int>(i), row[i]);
    }
    if (!query.exec()) {
      return std::unexpected(errorOf(query));
    }
  }
  return {};
}

// Visits each row of a SELECT. A failure part-way through is an error, not the
// end of the rows.
template <class Visit>
Result<void> selectRows(QSqlDatabase& db, const QString& sql,
                        const QVariantList& params, Visit&& visit) {
  auto query = runStatement(db, sql, params);
  if (!query) {
    return std::unexpected(query.error());
  }
  while (query->next()) {
    visit(*query);
  }
  if (query->lastError().isValid()) {
    return std::unexpected(errorOf(*query));
  }
  return {};
}

// Runs `body` inside one transaction: commits when it succeeds, rolls back
// otherwise.
template <class F>
auto inTransaction(QSqlDatabase& db, F&& body) {
  using R = std::invoke_result_t<F&>;
  if (!db.transaction()) {
    return R(std::unexpected(Error{db.lastError().text()}));
  }
  R result = body();
  if (!result) {
    db.rollback();
    return result;
  }
  if (!db.commit()) {
    Error error{db.lastError().text()};
    db.rollback();
    return R(std::unexpected(std::move(error)));
  }
  return result;
}

// Runs an UPDATE or DELETE that must touch a row; `missing` is the error when
// none matched.
Result<void> runExpectingRow(QSqlDatabase& db, const QString& sql,
                             const QVariantList& params,
                             const QString& missing) {
  auto query = runStatement(db, sql, params);
  if (!query) {
    return std::unexpected(query.error());
  }
  if (query->numRowsAffected() == 0) {
    return std::unexpected(Error{missing});
  }
  return {};
}

QString folderMissing(qint64 folderId) {
  return QStringLiteral("Folder with id %1 does not exist.").arg(folderId);
}

QString tagMissing(qint64 tagId) {
  return QStringLiteral("Tag with id %1 does not exist.").arg(tagId);
}

Error depthLimitError() {
  return Error{QStringLiteral("Folders can be nested at most %1 levels deep.")
                   .arg(kMaxDepth)};
}

Result<int> folderDepth(QSqlDatabase& db, qint64 folderId,
                        std::optional<qint64> movingFolder = std::nullopt) {
  QSqlQuery query(db);
  if (!query.prepare(
          QStringLiteral("SELECT parent_id FROM folders WHERE id = ?"))) {
    return std::unexpected(errorOf(query));
  }
  for (int depth = 0; depth <= kMaxDepth; ++depth) {
    if (folderId == movingFolder) {
      return std::unexpected(
          Error{QStringLiteral("A folder cannot be moved into itself.")});
    }
    query.bindValue(0, folderId);
    if (!query.exec()) {
      return std::unexpected(errorOf(query));
    }
    if (!query.next()) {
      if (query.lastError().isValid()) {
        return std::unexpected(errorOf(query));
      }
      return std::unexpected(
          Error{QStringLiteral("That folder no longer exists.")});
    }
    if (query.value(0).isNull()) {
      return depth;
    }
    folderId = query.value(0).toLongLong();
    query.finish();
  }
  return std::unexpected(depthLimitError());
}

Result<void> checkFolderMove(QSqlDatabase& db, qint64 folderId,
                             qint64 parentId) {
  auto parentDepth = folderDepth(db, parentId, folderId);
  if (!parentDepth) {
    return std::unexpected(parentDepth.error());
  }
  auto height = runStatement(
      db,
      QStringLiteral("WITH RECURSIVE subtree(id, height) AS ("
                     " SELECT id, 1 FROM folders WHERE id = ?"
                     " UNION ALL"
                     " SELECT f.id, s.height + 1 FROM folders f"
                     " JOIN subtree s ON f.parent_id = s.id WHERE s.height <= ?"
                     ") SELECT MAX(height) FROM subtree"),
      {folderId, kMaxDepth});
  if (!height) {
    return std::unexpected(height.error());
  }
  if (!height->next()) {
    return std::unexpected(errorOf(*height));
  }
  if (height->value(0).isNull()) {
    return std::unexpected(
        Error{QStringLiteral("That folder no longer exists.")});
  }
  if (*parentDepth + height->value(0).toInt() > kMaxDepth) {
    return std::unexpected(depthLimitError());
  }
  return {};
}

Result<OrganizerState> loadState(QSqlDatabase& db) {
  OrganizerState state;

  auto folders = selectRows(
      db, QStringLiteral("SELECT id, parent_id, name, collapsed FROM folders"),
      {}, [&state](QSqlQuery& row) {
        Folder folder;
        folder.id = row.value(0).toLongLong();
        if (!row.value(1).isNull()) {
          folder.parentId = row.value(1).toLongLong();
        }
        folder.name = row.value(2).toString();
        folder.collapsed = row.value(3).toLongLong() != 0;
        state.folders.insert(folder.id, folder);
      });
  if (!folders) {
    return std::unexpected(folders.error());
  }

  auto placements = selectRows(
      db, QStringLiteral("SELECT package_id, folder_id FROM placements"), {},
      [&state](QSqlQuery& row) {
        state.placements[row.value(0).toString().toLower()] =
            row.value(1).toLongLong();
      });
  if (!placements) {
    return std::unexpected(placements.error());
  }

  auto tags = selectRows(db, QStringLiteral("SELECT id, name, color FROM tags"),
                         {}, [&state](QSqlQuery& row) {
                           const Tag tag{.id = row.value(0).toLongLong(),
                                         .name = row.value(1).toString(),
                                         .color = row.value(2).toString()};
                           state.tags.insert(tag.id, tag);
                         });
  if (!tags) {
    return std::unexpected(tags.error());
  }

  auto modTags =
      selectRows(db, QStringLiteral("SELECT package_id, tag_id FROM mod_tags"),
                 {}, [&state](QSqlQuery& row) {
                   state.modTags[row.value(0).toString().toLower()].insert(
                       row.value(1).toLongLong());
                 });
  if (!modTags) {
    return std::unexpected(modTags.error());
  }

  bool corruptRule = false;
  auto rules = selectRows(
      db,
      QStringLiteral(
          "SELECT id, position, field, op, pattern, folder_id FROM rules "
          "ORDER BY position ASC, id ASC"),
      {}, [&state, &corruptRule](QSqlQuery& row) {
        const auto field = parseRuleField(row.value(2).toString());
        const auto op = parseRuleOp(row.value(3).toString());
        if (!field || !op) {
          corruptRule = true;
          return;
        }
        state.rules.append(Rule{.id = row.value(0).toLongLong(),
                                .position = row.value(1).toLongLong(),
                                .field = *field,
                                .op = *op,
                                .pattern = row.value(4).toString(),
                                .folderId = row.value(5).toLongLong()});
      });
  if (!rules) {
    return std::unexpected(rules.error());
  }
  if (corruptRule) {
    return std::unexpected(Error{QStringLiteral(
        "The organizer database holds an unknown rule field or operator.")});
  }
  return state;
}

Result<Folder> insertFolder(QSqlDatabase& db, qint64 parentId,
                            const QString& name) {
  auto parentDepth = folderDepth(db, parentId);
  if (!parentDepth) {
    return std::unexpected(parentDepth.error());
  }
  if (*parentDepth + 1 > kMaxDepth) {
    return std::unexpected(depthLimitError());
  }
  auto inserted = runStatement(
      db, QStringLiteral("INSERT INTO folders (parent_id, name) VALUES (?, ?)"),
      {parentId, name});
  if (!inserted) {
    return std::unexpected(inserted.error());
  }
  return Folder{.id = inserted->lastInsertId().toLongLong(),
                .parentId = parentId,
                .name = name,
                .collapsed = false};
}

Result<void> upsertPlacements(QSqlDatabase& db, const QStringList& packageIds,
                              qint64 folderId) {
  QList<QVariantList> rows;
  for (const QString& pid : packageIds) {
    rows.append({pid, folderId});
  }
  return runBatch(db, kPlaceSql, rows);
}

Result<Tag> insertTag(QSqlDatabase& db, const QString& name,
                      const QString& color) {
  auto inserted = runStatement(
      db, QStringLiteral("INSERT INTO tags (name, color) VALUES (?, ?)"),
      {name, color});
  if (!inserted) {
    return std::unexpected(inserted.error());
  }
  return Tag{.id = inserted->lastInsertId().toLongLong(),
             .name = name,
             .color = color};
}

Result<void> deleteFolderRows(QSqlDatabase& db, qint64 folderId) {
  bool exists = false;
  auto found =
      selectRows(db, QStringLiteral("SELECT 1 FROM folders WHERE id = ?"),
                 {folderId}, [&exists](QSqlQuery&) { exists = true; });
  if (!found) {
    return std::unexpected(found.error());
  }
  if (!exists) {
    return std::unexpected(Error{folderMissing(folderId)});
  }
  auto moved = runStatement(db, kMoveSubtreeSql, {folderId, kRootId});
  if (!moved) {
    return std::unexpected(moved.error());
  }
  auto deleted = runStatement(
      db, QStringLiteral("DELETE FROM folders WHERE id = ?"), {folderId});
  if (!deleted) {
    return std::unexpected(deleted.error());
  }
  return {};
}

Result<void> setModTagRows(QSqlDatabase& db, const QStringList& packageIds,
                           const QSet<qint64>& add,
                           const QSet<qint64>& remove) {
  if (!remove.isEmpty()) {
    QList<QVariantList> rows;
    for (const QString& pid : packageIds) {
      for (qint64 tagId : remove) {
        rows.append({pid, tagId});
      }
    }
    if (auto removed = runBatch(
            db,
            QStringLiteral(
                "DELETE FROM mod_tags WHERE package_id = ? AND tag_id = ?"),
            rows);
        !removed) {
      return removed;
    }
  }
  if (!add.isEmpty()) {
    QList<QVariantList> rows;
    for (const QString& pid : packageIds) {
      for (qint64 tagId : add) {
        rows.append({pid, tagId});
      }
    }
    if (auto added =
            runBatch(db,
                     QStringLiteral("INSERT OR IGNORE INTO mod_tags "
                                    "(package_id, tag_id) VALUES (?, ?)"),
                     rows);
        !added) {
      return added;
    }
  }
  return {};
}

Result<QList<Rule>> replaceRuleRows(QSqlDatabase& db,
                                    const QList<RuleSpec>& specs) {
  if (auto deleted = runStatement(db, QStringLiteral("DELETE FROM rules"));
      !deleted) {
    return std::unexpected(deleted.error());
  }
  QList<Rule> rules;
  for (qsizetype position = 0; position < specs.size(); ++position) {
    const RuleSpec& spec = specs[position];
    auto inserted = runStatement(
        db, kInsertRuleSql,
        {static_cast<qint64>(position), ruleFieldName(spec.field).toString(),
         ruleOpName(spec.op).toString(), spec.pattern, spec.folderId});
    if (!inserted) {
      return std::unexpected(inserted.error());
    }
    rules.append(Rule{.id = inserted->lastInsertId().toLongLong(),
                      .position = static_cast<qint64>(position),
                      .field = spec.field,
                      .op = spec.op,
                      .pattern = spec.pattern,
                      .folderId = spec.folderId});
  }
  return rules;
}

Result<void> appendNamedRuleRows(QSqlDatabase& db, qint64 parentId,
                                 const QStringList& newFolders,
                                 const QList<StandardRule>& rules) {
  if (!newFolders.isEmpty()) {
    auto parentDepth = folderDepth(db, parentId);
    if (!parentDepth) {
      return std::unexpected(parentDepth.error());
    }
    if (*parentDepth + 1 > kMaxDepth) {
      return std::unexpected(depthLimitError());
    }
  }
  QList<QVariantList> folderRows;
  for (const QString& name : newFolders) {
    folderRows.append({parentId, name});
  }
  if (auto created = runBatch(
          db,
          QStringLiteral("INSERT INTO folders (parent_id, name) VALUES (?, ?)"),
          folderRows);
      !created) {
    return created;
  }

  QHash<QString, qint64> folderIds;
  if (auto found = selectRows(
          db,
          QStringLiteral("SELECT id, name FROM folders WHERE parent_id = ?"),
          {parentId},
          [&folderIds](QSqlQuery& row) {
            folderIds[row.value(1).toString().toCaseFolded()] =
                row.value(0).toLongLong();
          });
      !found) {
    return found;
  }

  qint64 start = 0;
  if (auto next = selectRows(
          db,
          QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM rules"),
          {}, [&start](QSqlQuery& row) { start = row.value(0).toLongLong(); });
      !next) {
    return next;
  }

  QList<QVariantList> ruleRows;
  for (qsizetype offset = 0; offset < rules.size(); ++offset) {
    const StandardRule& rule = rules[offset];
    const auto folder = folderIds.constFind(rule.folder.toCaseFolded());
    if (folder == folderIds.cend()) {
      return std::unexpected(Error{
          QStringLiteral("Folder \"%1\" does not exist.").arg(rule.folder)});
    }
    ruleRows.append({start + static_cast<qint64>(offset),
                     ruleFieldName(rule.field).toString(),
                     ruleOpName(rule.op).toString(), rule.pattern,
                     folder.value()});
  }
  return runBatch(db, kInsertRuleSql, ruleRows);
}

// Opens the connection named `name`, creating the file and schema on first use.
// Runs on the worker thread only.
Result<void> configureConnection(QSqlDatabase& db, const QString& path) {
  if (!db.open()) {
    return std::unexpected(Error{db.lastError().text()});
  }
  // WAL keeps readers from blocking the writer; the busy timeout absorbs brief
  // contention.
  for (const char* pragma :
       {"PRAGMA journal_mode=WAL", "PRAGMA busy_timeout=5000",
        "PRAGMA foreign_keys=ON"}) {
    if (auto applied = runStatement(db, QString::fromLatin1(pragma));
        !applied) {
      return std::unexpected(applied.error());
    }
  }
  return ensureSchema(db, kSchema, kSchemaVersion, {}, path,
                      QStringLiteral("organizer DB"));
}

Result<QSqlDatabase> openConnection(const QString& name, const QString& path) {
  if (QSqlDatabase::contains(name)) {
    return QSqlDatabase::database(name, false);
  }
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return std::unexpected(
        Error{QStringLiteral("Cannot create the directory for %1").arg(path)});
  }
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
  db.setDatabaseName(path);
  if (auto ready = configureConnection(db, path); !ready) {
    // Drop the handle before removing the connection, so a retry can open it
    // again.
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
    return std::unexpected(ready.error());
  }
  return db;
}

void discardConnection(const QString& name) {
  if (!QSqlDatabase::contains(name)) {
    return;
  }
  {
    QSqlDatabase db = QSqlDatabase::database(name, false);
    db.close();
  }
  QSqlDatabase::removeDatabase(name);
}

}  // namespace

QString dbPath(const QString& configDir) {
  return QDir(configDir).filePath(QStringLiteral("organizer.db"));
}

OrganizerDb::OrganizerDb(QString path)
    : m_path(std::move(path)),
      m_connectionName(
          QStringLiteral("organizer-%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {
  m_pool.setMaxThreadCount(1);
  m_pool.setExpiryTimeout(-1);
}

OrganizerDb::~OrganizerDb() {
  // Blocks until the worker has released the connection; the pool cannot stop
  // before that.
  QtConcurrent::run(&m_pool, [name = m_connectionName] {
    discardConnection(name);
  }).waitForFinished();
}

template <class F>
auto OrganizerDb::submit(F fn) {
  using R = std::invoke_result_t<F&, QSqlDatabase&>;
  return QtConcurrent::run(
      &m_pool,
      [name = m_connectionName, path = m_path, fn = std::move(fn)]() -> R {
        auto db = openConnection(name, path);
        if (!db) {
          return std::unexpected(db.error());
        }
        return fn(*db);
      });
}

QCoro::Task<Result<OrganizerState>> OrganizerDb::load() {
  co_return co_await submit(
      [](QSqlDatabase& db) -> Result<OrganizerState> { return loadState(db); });
}

QCoro::Task<Result<Folder>> OrganizerDb::createFolder(qint64 parentId,
                                                      QString name) {
  co_return co_await submit(
      [parentId, name](QSqlDatabase& db) -> Result<Folder> {
        return inTransaction(db, [&]() -> Result<Folder> {
          return insertFolder(db, parentId, name);
        });
      });
}

QCoro::Task<Result<Folder>> OrganizerDb::createFolderWith(
    qint64 parentId, QString name, QStringList packageIds) {
  co_return co_await submit(
      [parentId, name,
       pids = cleanPackageIds(packageIds)](QSqlDatabase& db) -> Result<Folder> {
        return inTransaction(db, [&]() -> Result<Folder> {
          auto folder = insertFolder(db, parentId, name);
          if (!folder) {
            return folder;
          }
          if (auto placed = upsertPlacements(db, pids, folder->id); !placed) {
            return std::unexpected(placed.error());
          }
          return folder;
        });
      });
}

QCoro::Task<Result<void>> OrganizerDb::renameFolder(qint64 folderId,
                                                    QString name) {
  if (folderId == kRootId) {
    co_return std::unexpected(
        Error{QStringLiteral("The root folder cannot be renamed.")});
  }
  co_return co_await submit([folderId, name](QSqlDatabase& db) -> Result<void> {
    return inTransaction(db, [&]() -> Result<void> {
      return runExpectingRow(
          db, QStringLiteral("UPDATE folders SET name = ? WHERE id = ?"),
          {name, folderId}, folderMissing(folderId));
    });
  });
}

QCoro::Task<Result<void>> OrganizerDb::setParent(qint64 folderId,
                                                 qint64 parentId) {
  if (folderId == kRootId) {
    co_return std::unexpected(
        Error{QStringLiteral("The root folder cannot be moved.")});
  }
  co_return co_await submit([folderId,
                             parentId](QSqlDatabase& db) -> Result<void> {
    return inTransaction(db, [&]() -> Result<void> {
      if (auto valid = checkFolderMove(db, folderId, parentId); !valid) {
        return valid;
      }
      return runExpectingRow(
          db, QStringLiteral("UPDATE folders SET parent_id = ? WHERE id = ?"),
          {parentId, folderId}, folderMissing(folderId));
    });
  });
}

QCoro::Task<Result<void>> OrganizerDb::setCollapsed(qint64 folderId,
                                                    bool collapsed) {
  co_return co_await submit([folderId,
                             collapsed](QSqlDatabase& db) -> Result<void> {
    return inTransaction(db, [&]() -> Result<void> {
      return runExpectingRow(
          db, QStringLiteral("UPDATE folders SET collapsed = ? WHERE id = ?"),
          {collapsed ? 1 : 0, folderId}, folderMissing(folderId));
    });
  });
}

QCoro::Task<Result<void>> OrganizerDb::setAllCollapsed(bool collapsed) {
  co_return co_await submit([collapsed](QSqlDatabase& db) -> Result<void> {
    return inTransaction(db, [&]() -> Result<void> {
      auto updated =
          runStatement(db, QStringLiteral("UPDATE folders SET collapsed = ?"),
                       {collapsed ? 1 : 0});
      if (!updated) {
        return std::unexpected(updated.error());
      }
      return {};
    });
  });
}

QCoro::Task<Result<void>> OrganizerDb::deleteFolder(qint64 folderId) {
  if (folderId == kRootId) {
    co_return std::unexpected(
        Error{QStringLiteral("The root folder cannot be deleted.")});
  }
  co_return co_await submit([folderId](QSqlDatabase& db) -> Result<void> {
    return inTransaction(
        db, [&]() -> Result<void> { return deleteFolderRows(db, folderId); });
  });
}

QCoro::Task<Result<void>> OrganizerDb::place(QStringList packageIds,
                                             qint64 folderId) {
  const QStringList pids = cleanPackageIds(packageIds);
  if (pids.isEmpty()) {
    co_return {};
  }
  co_return co_await submit([pids, folderId](QSqlDatabase& db) -> Result<void> {
    return inTransaction(db, [&]() -> Result<void> {
      return upsertPlacements(db, pids, folderId);
    });
  });
}

QCoro::Task<Result<void>> OrganizerDb::unplace(QStringList packageIds) {
  const QStringList pids = cleanPackageIds(packageIds);
  if (pids.isEmpty()) {
    co_return {};
  }
  co_return co_await submit([pids](QSqlDatabase& db) -> Result<void> {
    return inTransaction(db, [&]() -> Result<void> {
      QList<QVariantList> rows;
      for (const QString& pid : pids) {
        rows.append({pid});
      }
      return runBatch(
          db, QStringLiteral("DELETE FROM placements WHERE package_id = ?"),
          rows);
    });
  });
}

QCoro::Task<Result<Tag>> OrganizerDb::createTag(QString name, QString color) {
  co_return co_await submit([name, color](QSqlDatabase& db) -> Result<Tag> {
    return inTransaction(
        db, [&]() -> Result<Tag> { return insertTag(db, name, color); });
  });
}

QCoro::Task<Result<void>> OrganizerDb::updateTag(qint64 tagId, QString name,
                                                 QString color) {
  co_return co_await submit([tagId, name,
                             color](QSqlDatabase& db) -> Result<void> {
    return inTransaction(db, [&]() -> Result<void> {
      return runExpectingRow(
          db,
          QStringLiteral("UPDATE tags SET name = ?, color = ? WHERE id = ?"),
          {name, color, tagId}, tagMissing(tagId));
    });
  });
}

QCoro::Task<Result<void>> OrganizerDb::deleteTag(qint64 tagId) {
  co_return co_await submit([tagId](QSqlDatabase& db) -> Result<void> {
    return inTransaction(db, [&]() -> Result<void> {
      return runExpectingRow(db,
                             QStringLiteral("DELETE FROM tags WHERE id = ?"),
                             {tagId}, tagMissing(tagId));
    });
  });
}

QCoro::Task<Result<void>> OrganizerDb::setModTags(QStringList packageIds,
                                                  QSet<qint64> add,
                                                  QSet<qint64> remove) {
  const QStringList pids = cleanPackageIds(packageIds);
  if (pids.isEmpty() || (add.isEmpty() && remove.isEmpty())) {
    co_return {};
  }
  co_return co_await submit(
      [pids, add, remove](QSqlDatabase& db) -> Result<void> {
        return inTransaction(db, [&]() -> Result<void> {
          return setModTagRows(db, pids, add, remove);
        });
      });
}

QCoro::Task<Result<QList<Rule>>> OrganizerDb::replaceRules(
    QList<RuleSpec> specs) {
  co_return co_await submit([specs](QSqlDatabase& db) -> Result<QList<Rule>> {
    return inTransaction(db, [&]() -> Result<QList<Rule>> {
      return replaceRuleRows(db, specs);
    });
  });
}

QCoro::Task<Result<void>> OrganizerDb::appendNamedRules(
    qint64 parentId, QStringList newFolders, QList<StandardRule> rules) {
  co_return co_await submit(
      [parentId, newFolders, rules](QSqlDatabase& db) -> Result<void> {
        return inTransaction(db, [&]() -> Result<void> {
          return appendNamedRuleRows(db, parentId, newFolders, rules);
        });
      });
}

QCoro::Task<void> OrganizerDb::close() {
  co_await QtConcurrent::run(
      &m_pool, [name = m_connectionName] { discardConnection(name); });
}

}  // namespace px::modrim::organizer
