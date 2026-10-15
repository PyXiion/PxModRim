#include "core/organizer/db.h"

#include <type_traits>
#include <utility>

#include <QCoro/QCoroFuture>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include "core/migrator.h"

namespace px::core::organizer {

namespace {

// Byte-for-byte the legacy schema: existing user databases carry this exact version-1 layout.
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
    "INSERT INTO rules (position, field, op, pattern, folder_id) VALUES (?, ?, ?, ?, ?)");
// Re-parents placements of the whole subtree to the root before the folders are deleted.
const QString kMoveSubtreeSql = QStringLiteral(
    "WITH RECURSIVE subtree(id) AS ("
    "    SELECT ?"
    "    UNION ALL"
    "    SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id"
    ") "
    "UPDATE placements SET folder_id = ? WHERE folder_id IN (SELECT id FROM subtree)");

Error error_of(const QSqlQuery& query) {
    return Error{query.lastError().text()};
}

Result<QSqlQuery> run_statement(QSqlDatabase& db, const QString& sql, const QVariantList& params = {}) {
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        return std::unexpected(error_of(query));
    }
    for (qsizetype i = 0; i < params.size(); ++i) {
        query.bindValue(static_cast<int>(i), params[i]);
    }
    if (!query.exec()) {
        return std::unexpected(error_of(query));
    }
    return query;
}

// Prepares `sql` once and runs it for each row. Positional values are rebound every time.
Result<void> run_batch(QSqlDatabase& db, const QString& sql, const QList<QVariantList>& rows) {
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        return std::unexpected(error_of(query));
    }
    for (const QVariantList& row : rows) {
        for (qsizetype i = 0; i < row.size(); ++i) {
            query.bindValue(static_cast<int>(i), row[i]);
        }
        if (!query.exec()) {
            return std::unexpected(error_of(query));
        }
    }
    return {};
}

// Visits each row of a SELECT. A failure part-way through is an error, not the end of the rows.
template <class Visit>
Result<void> select_rows(QSqlDatabase& db, const QString& sql, const QVariantList& params, Visit&& visit) {
    auto query = run_statement(db, sql, params);
    if (!query) {
        return std::unexpected(query.error());
    }
    while (query->next()) {
        visit(*query);
    }
    if (query->lastError().isValid()) {
        return std::unexpected(error_of(*query));
    }
    return {};
}

// Runs `body` inside one transaction: commits when it succeeds, rolls back otherwise.
template <class F>
auto in_transaction(QSqlDatabase& db, F&& body) {
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

// Runs an UPDATE or DELETE that must touch a row; `missing` is the error when none matched.
Result<void> run_expecting_row(QSqlDatabase& db, const QString& sql, const QVariantList& params,
                               const QString& missing) {
    auto query = run_statement(db, sql, params);
    if (!query) {
        return std::unexpected(query.error());
    }
    if (query->numRowsAffected() == 0) {
        return std::unexpected(Error{missing});
    }
    return {};
}

QString folder_missing(qint64 folder_id) {
    return QStringLiteral("Folder with id %1 does not exist.").arg(folder_id);
}

QString tag_missing(qint64 tag_id) {
    return QStringLiteral("Tag with id %1 does not exist.").arg(tag_id);
}

Result<OrganizerState> load_state(QSqlDatabase& db) {
    OrganizerState state;

    auto folders = select_rows(db, QStringLiteral("SELECT id, parent_id, name, collapsed FROM folders"), {},
                               [&state](QSqlQuery& row) {
                                   Folder folder;
                                   folder.id = row.value(0).toLongLong();
                                   if (!row.value(1).isNull()) {
                                       folder.parent_id = row.value(1).toLongLong();
                                   }
                                   folder.name = row.value(2).toString();
                                   folder.collapsed = row.value(3).toLongLong() != 0;
                                   state.folders.insert(folder.id, folder);
                               });
    if (!folders) {
        return std::unexpected(folders.error());
    }

    auto placements = select_rows(db, QStringLiteral("SELECT package_id, folder_id FROM placements"), {},
                                  [&state](QSqlQuery& row) {
                                      state.placements[row.value(0).toString().toLower()] =
                                          row.value(1).toLongLong();
                                  });
    if (!placements) {
        return std::unexpected(placements.error());
    }

    auto tags = select_rows(db, QStringLiteral("SELECT id, name, color FROM tags"), {}, [&state](QSqlQuery& row) {
        const Tag tag{.id = row.value(0).toLongLong(),
                      .name = row.value(1).toString(),
                      .color = row.value(2).toString()};
        state.tags.insert(tag.id, tag);
    });
    if (!tags) {
        return std::unexpected(tags.error());
    }

    auto mod_tags = select_rows(db, QStringLiteral("SELECT package_id, tag_id FROM mod_tags"), {},
                                [&state](QSqlQuery& row) {
                                    state.mod_tags[row.value(0).toString().toLower()].insert(
                                        row.value(1).toLongLong());
                                });
    if (!mod_tags) {
        return std::unexpected(mod_tags.error());
    }

    bool corrupt_rule = false;
    auto rules = select_rows(
        db,
        QStringLiteral("SELECT id, position, field, op, pattern, folder_id FROM rules "
                       "ORDER BY position ASC, id ASC"),
        {}, [&state, &corrupt_rule](QSqlQuery& row) {
            const auto field = parse_rule_field(row.value(2).toString());
            const auto op = parse_rule_op(row.value(3).toString());
            if (!field || !op) {
                corrupt_rule = true;
                return;
            }
            state.rules.append(Rule{.id = row.value(0).toLongLong(),
                                    .position = row.value(1).toLongLong(),
                                    .field = *field,
                                    .op = *op,
                                    .pattern = row.value(4).toString(),
                                    .folder_id = row.value(5).toLongLong()});
        });
    if (!rules) {
        return std::unexpected(rules.error());
    }
    if (corrupt_rule) {
        return std::unexpected(Error{QStringLiteral("The organizer database holds an unknown rule field or operator.")});
    }
    return state;
}

Result<Folder> insert_folder(QSqlDatabase& db, qint64 parent_id, const QString& name) {
    auto inserted = run_statement(db, QStringLiteral("INSERT INTO folders (parent_id, name) VALUES (?, ?)"),
                                  {parent_id, name});
    if (!inserted) {
        return std::unexpected(inserted.error());
    }
    return Folder{.id = inserted->lastInsertId().toLongLong(),
                  .parent_id = parent_id,
                  .name = name,
                  .collapsed = false};
}

Result<void> upsert_placements(QSqlDatabase& db, const QStringList& package_ids, qint64 folder_id) {
    QList<QVariantList> rows;
    for (const QString& pid : package_ids) {
        rows.append({pid, folder_id});
    }
    return run_batch(db, kPlaceSql, rows);
}

Result<Tag> insert_tag(QSqlDatabase& db, const QString& name, const QString& color) {
    auto inserted = run_statement(db, QStringLiteral("INSERT INTO tags (name, color) VALUES (?, ?)"), {name, color});
    if (!inserted) {
        return std::unexpected(inserted.error());
    }
    return Tag{.id = inserted->lastInsertId().toLongLong(), .name = name, .color = color};
}

Result<void> delete_folder_rows(QSqlDatabase& db, qint64 folder_id) {
    bool exists = false;
    auto found = select_rows(db, QStringLiteral("SELECT 1 FROM folders WHERE id = ?"), {folder_id},
                             [&exists](QSqlQuery&) { exists = true; });
    if (!found) {
        return std::unexpected(found.error());
    }
    if (!exists) {
        return std::unexpected(Error{folder_missing(folder_id)});
    }
    auto moved = run_statement(db, kMoveSubtreeSql, {folder_id, ROOT_ID});
    if (!moved) {
        return std::unexpected(moved.error());
    }
    auto deleted = run_statement(db, QStringLiteral("DELETE FROM folders WHERE id = ?"), {folder_id});
    if (!deleted) {
        return std::unexpected(deleted.error());
    }
    return {};
}

Result<void> set_mod_tag_rows(QSqlDatabase& db, const QStringList& package_ids, const QSet<qint64>& add,
                              const QSet<qint64>& remove) {
    if (!remove.isEmpty()) {
        QList<QVariantList> rows;
        for (const QString& pid : package_ids) {
            for (qint64 tag_id : remove) {
                rows.append({pid, tag_id});
            }
        }
        if (auto removed = run_batch(db, QStringLiteral("DELETE FROM mod_tags WHERE package_id = ? AND tag_id = ?"), rows);
            !removed) {
            return removed;
        }
    }
    if (!add.isEmpty()) {
        QList<QVariantList> rows;
        for (const QString& pid : package_ids) {
            for (qint64 tag_id : add) {
                rows.append({pid, tag_id});
            }
        }
        if (auto added = run_batch(db, QStringLiteral("INSERT OR IGNORE INTO mod_tags (package_id, tag_id) VALUES (?, ?)"), rows);
            !added) {
            return added;
        }
    }
    return {};
}

Result<QList<Rule>> replace_rule_rows(QSqlDatabase& db, const QList<RuleSpec>& specs) {
    if (auto deleted = run_statement(db, QStringLiteral("DELETE FROM rules")); !deleted) {
        return std::unexpected(deleted.error());
    }
    QList<Rule> rules;
    for (qsizetype position = 0; position < specs.size(); ++position) {
        const RuleSpec& spec = specs[position];
        auto inserted = run_statement(db, kInsertRuleSql,
                                      {static_cast<qint64>(position), rule_field_name(spec.field).toString(),
                                       rule_op_name(spec.op).toString(), spec.pattern, spec.folder_id});
        if (!inserted) {
            return std::unexpected(inserted.error());
        }
        rules.append(Rule{.id = inserted->lastInsertId().toLongLong(),
                          .position = static_cast<qint64>(position),
                          .field = spec.field,
                          .op = spec.op,
                          .pattern = spec.pattern,
                          .folder_id = spec.folder_id});
    }
    return rules;
}

Result<void> append_named_rule_rows(QSqlDatabase& db, qint64 parent_id, const QStringList& new_folders,
                                    const QList<StandardRule>& rules) {
    QList<QVariantList> folder_rows;
    for (const QString& name : new_folders) {
        folder_rows.append({parent_id, name});
    }
    if (auto created = run_batch(db, QStringLiteral("INSERT INTO folders (parent_id, name) VALUES (?, ?)"), folder_rows);
        !created) {
        return created;
    }

    QHash<QString, qint64> folder_ids;
    if (auto found = select_rows(db, QStringLiteral("SELECT id, name FROM folders WHERE parent_id = ?"),
                                 {parent_id}, [&folder_ids](QSqlQuery& row) {
                                     folder_ids[row.value(1).toString().toCaseFolded()] = row.value(0).toLongLong();
                                 });
        !found) {
        return found;
    }

    qint64 start = 0;
    if (auto next = select_rows(db, QStringLiteral("SELECT COALESCE(MAX(position), -1) + 1 FROM rules"), {},
                                [&start](QSqlQuery& row) { start = row.value(0).toLongLong(); });
        !next) {
        return next;
    }

    QList<QVariantList> rule_rows;
    for (qsizetype offset = 0; offset < rules.size(); ++offset) {
        const StandardRule& rule = rules[offset];
        const auto folder = folder_ids.constFind(rule.folder.toCaseFolded());
        if (folder == folder_ids.cend()) {
            return std::unexpected(Error{QStringLiteral("Folder \"%1\" does not exist.").arg(rule.folder)});
        }
        rule_rows.append({start + static_cast<qint64>(offset), rule_field_name(rule.field).toString(),
                          rule_op_name(rule.op).toString(), rule.pattern, folder.value()});
    }
    return run_batch(db, kInsertRuleSql, rule_rows);
}

// Opens the connection named `name`, creating the file and schema on first use. Runs on the
// worker thread only.
Result<void> configure_connection(QSqlDatabase& db, const QString& path) {
    if (!db.open()) {
        return std::unexpected(Error{db.lastError().text()});
    }
    // WAL keeps readers from blocking the writer; the busy timeout absorbs brief contention.
    for (const char* pragma : {"PRAGMA journal_mode=WAL", "PRAGMA busy_timeout=5000", "PRAGMA foreign_keys=ON"}) {
        if (auto applied = run_statement(db, QString::fromLatin1(pragma)); !applied) {
            return std::unexpected(applied.error());
        }
    }
    return ensure_schema(db, kSchema, kSchemaVersion, {}, path, QStringLiteral("organizer DB"));
}

Result<QSqlDatabase> open_connection(const QString& name, const QString& path) {
    if (QSqlDatabase::contains(name)) {
        return QSqlDatabase::database(name, false);
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return std::unexpected(Error{QStringLiteral("Cannot create the directory for %1").arg(path)});
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(path);
    if (auto ready = configure_connection(db, path); !ready) {
        // Drop the handle before removing the connection, so a retry can open it again.
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
        return std::unexpected(ready.error());
    }
    return db;
}

void discard_connection(const QString& name) {
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

QString db_path(const QString& config_dir) {
    return QDir(config_dir).filePath(QStringLiteral("organizer.db"));
}

OrganizerDb::OrganizerDb(QString path)
    : path_(std::move(path)),
      connection_name_(QStringLiteral("organizer-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(-1);
}

OrganizerDb::~OrganizerDb() {
    // Blocks until the worker has released the connection; the pool cannot stop before that.
    QtConcurrent::run(&pool_, [name = connection_name_] { discard_connection(name); }).waitForFinished();
}

template <class F>
auto OrganizerDb::submit(F fn) {
    using R = std::invoke_result_t<F&, QSqlDatabase&>;
    return QtConcurrent::run(&pool_, [name = connection_name_, path = path_, fn = std::move(fn)]() -> R {
        auto db = open_connection(name, path);
        if (!db) {
            return std::unexpected(db.error());
        }
        return fn(*db);
    });
}

QCoro::Task<Result<OrganizerState>> OrganizerDb::load() {
    co_return co_await submit([](QSqlDatabase& db) -> Result<OrganizerState> { return load_state(db); });
}

QCoro::Task<Result<Folder>> OrganizerDb::create_folder(qint64 parent_id, QString name) {
    co_return co_await submit([parent_id, name](QSqlDatabase& db) -> Result<Folder> {
        return in_transaction(db, [&]() -> Result<Folder> { return insert_folder(db, parent_id, name); });
    });
}

QCoro::Task<Result<Folder>> OrganizerDb::create_folder_with(qint64 parent_id, QString name,
                                                            QStringList package_ids) {
    co_return co_await submit([parent_id, name, pids = clean_package_ids(package_ids)](QSqlDatabase& db) -> Result<Folder> {
        return in_transaction(db, [&]() -> Result<Folder> {
            auto folder = insert_folder(db, parent_id, name);
            if (!folder) {
                return folder;
            }
            if (auto placed = upsert_placements(db, pids, folder->id); !placed) {
                return std::unexpected(placed.error());
            }
            return folder;
        });
    });
}

QCoro::Task<Result<void>> OrganizerDb::rename_folder(qint64 folder_id, QString name) {
    if (folder_id == ROOT_ID) {
        co_return std::unexpected(Error{QStringLiteral("The root folder cannot be renamed.")});
    }
    co_return co_await submit([folder_id, name](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> {
            return run_expecting_row(db, QStringLiteral("UPDATE folders SET name = ? WHERE id = ?"),
                                     {name, folder_id}, folder_missing(folder_id));
        });
    });
}

QCoro::Task<Result<void>> OrganizerDb::set_parent(qint64 folder_id, qint64 parent_id) {
    if (folder_id == ROOT_ID) {
        co_return std::unexpected(Error{QStringLiteral("The root folder cannot be moved.")});
    }
    if (folder_id == parent_id) {
        co_return std::unexpected(Error{QStringLiteral("A folder cannot be its own parent.")});
    }
    co_return co_await submit([folder_id, parent_id](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> {
            return run_expecting_row(db, QStringLiteral("UPDATE folders SET parent_id = ? WHERE id = ?"),
                                     {parent_id, folder_id}, folder_missing(folder_id));
        });
    });
}

QCoro::Task<Result<void>> OrganizerDb::set_collapsed(qint64 folder_id, bool collapsed) {
    co_return co_await submit([folder_id, collapsed](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> {
            return run_expecting_row(db, QStringLiteral("UPDATE folders SET collapsed = ? WHERE id = ?"),
                                     {collapsed ? 1 : 0, folder_id}, folder_missing(folder_id));
        });
    });
}

QCoro::Task<Result<void>> OrganizerDb::set_all_collapsed(bool collapsed) {
    co_return co_await submit([collapsed](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> {
            auto updated = run_statement(db, QStringLiteral("UPDATE folders SET collapsed = ?"),
                                         {collapsed ? 1 : 0});
            if (!updated) {
                return std::unexpected(updated.error());
            }
            return {};
        });
    });
}

QCoro::Task<Result<void>> OrganizerDb::delete_folder(qint64 folder_id) {
    if (folder_id == ROOT_ID) {
        co_return std::unexpected(Error{QStringLiteral("The root folder cannot be deleted.")});
    }
    co_return co_await submit([folder_id](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> { return delete_folder_rows(db, folder_id); });
    });
}

QCoro::Task<Result<void>> OrganizerDb::place(QStringList package_ids, qint64 folder_id) {
    const QStringList pids = clean_package_ids(package_ids);
    if (pids.isEmpty()) {
        co_return {};
    }
    co_return co_await submit([pids, folder_id](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> { return upsert_placements(db, pids, folder_id); });
    });
}

QCoro::Task<Result<void>> OrganizerDb::unplace(QStringList package_ids) {
    const QStringList pids = clean_package_ids(package_ids);
    if (pids.isEmpty()) {
        co_return {};
    }
    co_return co_await submit([pids](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> {
            QList<QVariantList> rows;
            for (const QString& pid : pids) {
                rows.append({pid});
            }
            return run_batch(db, QStringLiteral("DELETE FROM placements WHERE package_id = ?"), rows);
        });
    });
}

QCoro::Task<Result<Tag>> OrganizerDb::create_tag(QString name, QString color) {
    co_return co_await submit([name, color](QSqlDatabase& db) -> Result<Tag> {
        return in_transaction(db, [&]() -> Result<Tag> { return insert_tag(db, name, color); });
    });
}

QCoro::Task<Result<void>> OrganizerDb::update_tag(qint64 tag_id, QString name, QString color) {
    co_return co_await submit([tag_id, name, color](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> {
            return run_expecting_row(db, QStringLiteral("UPDATE tags SET name = ?, color = ? WHERE id = ?"),
                                     {name, color, tag_id}, tag_missing(tag_id));
        });
    });
}

QCoro::Task<Result<void>> OrganizerDb::delete_tag(qint64 tag_id) {
    co_return co_await submit([tag_id](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> {
            return run_expecting_row(db, QStringLiteral("DELETE FROM tags WHERE id = ?"), {tag_id},
                                     tag_missing(tag_id));
        });
    });
}

QCoro::Task<Result<void>> OrganizerDb::set_mod_tags(QStringList package_ids, QSet<qint64> add,
                                                    QSet<qint64> remove) {
    const QStringList pids = clean_package_ids(package_ids);
    if (pids.isEmpty() || (add.isEmpty() && remove.isEmpty())) {
        co_return {};
    }
    co_return co_await submit([pids, add, remove](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> { return set_mod_tag_rows(db, pids, add, remove); });
    });
}

QCoro::Task<Result<QList<Rule>>> OrganizerDb::replace_rules(QList<RuleSpec> specs) {
    co_return co_await submit([specs](QSqlDatabase& db) -> Result<QList<Rule>> {
        return in_transaction(db, [&]() -> Result<QList<Rule>> { return replace_rule_rows(db, specs); });
    });
}

QCoro::Task<Result<void>> OrganizerDb::append_named_rules(qint64 parent_id, QStringList new_folders,
                                                          QList<StandardRule> rules) {
    co_return co_await submit([parent_id, new_folders, rules](QSqlDatabase& db) -> Result<void> {
        return in_transaction(db, [&]() -> Result<void> {
            return append_named_rule_rows(db, parent_id, new_folders, rules);
        });
    });
}

QCoro::Task<void> OrganizerDb::close() {
    co_await QtConcurrent::run(&pool_, [name = connection_name_] { discard_connection(name); });
}

}  // namespace px::core::organizer
