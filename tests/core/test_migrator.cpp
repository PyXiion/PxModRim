#include <catch2/catch_test_macros.hpp>

#include <string>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include "core/migrator.h"
#include "test_support.h"

namespace {

using px::modrim::ensure_schema;
using px::modrim::MigrationSteps;
using px::modrim::Result;
using test_support::read_bytes;

// Owns one named SQLite connection and closes it when it goes out of scope.
class Database {
public:
    Database(const QString& path, QString name) : name_(std::move(name)) {
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name_);
        db_.setDatabaseName(path);
        REQUIRE(db_.open());
    }

    ~Database() {
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(name_);
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    QSqlDatabase& get() { return db_; }

    void exec(const QString& sql) {
        QSqlQuery query(db_);
        REQUIRE(query.exec(sql));
    }

    QStringList column(const QString& sql) {
        QSqlQuery query(db_);
        REQUIRE(query.exec(sql));
        QStringList result;
        while (query.next()) {
            result.append(query.value(0).toString());
        }
        return result;
    }

    int user_version() {
        QSqlQuery query(db_);
        REQUIRE(query.exec(QStringLiteral("PRAGMA user_version")));
        REQUIRE(query.next());
        return query.value(0).toInt();
    }

private:
    QSqlDatabase db_;
    QString name_;
};

QStringList backups_of(const QString& dir, const QString& db_name) {
    return QDir(dir).entryList({db_name + QStringLiteral(".bak.*")}, QDir::Files);
}

Result<void> noop() { return {}; }

}  // namespace

TEST_CASE("fresh database is initialised and stamped without running steps") {
    test_support::TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cache.db"));
    bool stepped = false;
    const MigrationSteps steps{
        {1, noop},
        {2, [&]() -> Result<void> {
             stepped = true;
             return {};
         }},
    };
    {
        Database db(path, QStringLiteral("fresh"));
        REQUIRE(ensure_schema(db.get(),
                              QStringLiteral("CREATE TABLE state (value TEXT DEFAULT 'a;b'); -- note; ignored\n"
                                             "CREATE TABLE other (x INTEGER);"),
                              2, steps, path)
                    .has_value());
        REQUIRE(db.user_version() == 2);
        REQUIRE(db.column(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'state'")) ==
                QStringList{"state"});
        REQUIRE(db.column(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'other'")) ==
                QStringList{"other"});
    }
    REQUIRE_FALSE(stepped);
    REQUIRE(backups_of(dir.path(), QStringLiteral("cache.db")).isEmpty());
}

TEST_CASE("migrations checkpoint the WAL before backing up") {
    test_support::TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cache.db"));
    QStringList events;
    {
        Database db(path, QStringLiteral("wal"));
        REQUIRE(db.column(QStringLiteral("PRAGMA journal_mode = WAL")) == QStringList{"wal"});
        db.exec(QStringLiteral("PRAGMA wal_autocheckpoint = 0"));
        db.exec(QStringLiteral("CREATE TABLE state (value TEXT)"));
        db.exec(QStringLiteral("INSERT INTO state VALUES ('before')"));
        db.exec(QStringLiteral("PRAGMA user_version = 1"));
        REQUIRE(QFileInfo(path + QStringLiteral("-wal")).size() > 0);

        const MigrationSteps steps{
            {2, [&]() -> Result<void> {
                 events.append(QStringLiteral("two"));
                 db.exec(QStringLiteral("ALTER TABLE state ADD COLUMN second TEXT"));
                 return {};
             }},
            {3, [&]() -> Result<void> {
                 events.append(QStringLiteral("three"));
                 db.exec(QStringLiteral("ALTER TABLE state ADD COLUMN third TEXT"));
                 return {};
             }},
        };
        REQUIRE(ensure_schema(db.get(), QStringLiteral("CREATE TABLE IF NOT EXISTS state (value TEXT);"), 3, steps,
                              path)
                    .has_value());
        REQUIRE(db.user_version() == 3);
        REQUIRE(db.column(QStringLiteral("SELECT name FROM pragma_table_info('state')")) ==
                QStringList{"value", "second", "third"});
        REQUIRE(events == QStringList{"two", "three"});
    }

    const auto found = backups_of(dir.path(), QStringLiteral("cache.db"));
    REQUIRE(found.size() == 1);
    const QString backup = dir.filePath(found.first());
    {
        Database backup_db(backup, QStringLiteral("wal-backup"));
        REQUIRE(backup_db.user_version() == 1);
        REQUIRE(backup_db.column(QStringLiteral("SELECT name FROM pragma_table_info('state')")) ==
                QStringList{"value"});
        REQUIRE(backup_db.column(QStringLiteral("SELECT value FROM state")) == QStringList{"before"});
    }
    for (const char* suffix : {"-wal", "-shm"}) {
        QFile::remove(backup + QString::fromLatin1(suffix));
    }
}

TEST_CASE("missing migration step fails without advancing the version") {
    test_support::TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cache.db"));
    std::string before;
    {
        Database db(path, QStringLiteral("missing"));
        db.exec(QStringLiteral("CREATE TABLE state (value TEXT)"));
        db.exec(QStringLiteral("PRAGMA user_version = 1"));
        before = read_bytes(path);
        const auto result = ensure_schema(db.get(),
                                          QStringLiteral("CREATE TABLE IF NOT EXISTS state (value TEXT);"), 3,
                                          MigrationSteps{{2, noop}}, path);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().message.contains("Missing migration step"));
        REQUIRE(db.user_version() == 1);
    }

    REQUIRE(read_bytes(path) == before);
    const auto found = backups_of(dir.path(), QStringLiteral("cache.db"));
    REQUIRE(found.size() == 1);
    REQUIRE(read_bytes(dir.filePath(found.first())) == before);
}

TEST_CASE("current schema is a no-op") {
    test_support::TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cache.db"));
    std::string before;
    bool called = false;
    {
        Database db(path, QStringLiteral("current"));
        db.exec(QStringLiteral("CREATE TABLE state (value TEXT)"));
        db.exec(QStringLiteral("PRAGMA user_version = 1"));
        before = read_bytes(path);
        const MigrationSteps steps{{1, [&]() -> Result<void> {
                                        called = true;
                                        return {};
                                    }}};
        REQUIRE(ensure_schema(db.get(), QStringLiteral("CREATE TABLE should_not_be_created (value TEXT);"), 1,
                              steps, path)
                    .has_value());
        REQUIRE(db.user_version() == 1);
    }
    REQUIRE_FALSE(called);
    REQUIRE(read_bytes(path) == before);
    REQUIRE(backups_of(dir.path(), QStringLiteral("cache.db")).isEmpty());
}

TEST_CASE("newer schema is a no-op") {
    test_support::TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cache.db"));
    std::string before;
    bool called = false;
    {
        Database db(path, QStringLiteral("newer"));
        db.exec(QStringLiteral("CREATE TABLE state (value TEXT)"));
        db.exec(QStringLiteral("PRAGMA user_version = 2"));
        before = read_bytes(path);
        const MigrationSteps steps{{1, [&]() -> Result<void> {
                                        called = true;
                                        return {};
                                    }}};
        REQUIRE(ensure_schema(db.get(), QStringLiteral("CREATE TABLE should_not_be_created (value TEXT);"), 1,
                              steps, path)
                    .has_value());
        REQUIRE(db.user_version() == 2);
    }
    REQUIRE_FALSE(called);
    REQUIRE(read_bytes(path) == before);
    REQUIRE(backups_of(dir.path(), QStringLiteral("cache.db")).isEmpty());
}
