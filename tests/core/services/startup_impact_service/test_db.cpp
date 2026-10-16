#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <type_traits>

#include <QCoro/QCoroTask>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "../../test_support.hpp"
#include "core/services/startup_impact_service/db.hpp"

using namespace px::modrim::startup_impact;
using Catch::Matchers::WithinAbs;
using px::modrim::Result;
using test_support::TestApplication;

namespace {

// Awaits a DB call on the event loop and fails the test on error.
template <class T>
auto ok(QCoro::Task<Result<T>> task) {
    auto result = QCoro::waitFor(std::move(task));
    if (!result.has_value()) {
        UNSCOPED_INFO("DB error: " << result.error().message.toStdString());
    }
    REQUIRE(result.has_value());
    if constexpr (std::is_void_v<T>) {
        return;
    } else {
        return std::move(*result);
    }
}

StartupImpactMod mod_entry(const QString& name, const std::optional<QString>& pid,
                           double total_s, double off_s = 0.0) {
    StartupImpactMod mod;
    mod.mod_name = name;
    mod.package_id = pid;
    mod.total_impact_s = total_s;
    mod.off_thread_total_impact_s = off_s;
    return mod;
}

StartupImpactReport make_report(const QString& pid, double total_s, double off_thread_s = 0.0) {
    StartupImpactReport report;
    report.loading_time_s = 1.0;
    report.timestamp = QStringLiteral("now");
    report.mods.append(mod_entry(QStringLiteral("Mod ") + pid, pid, total_s, off_thread_s));
    return report;
}

QString db_file(const QTemporaryDir& dir) { return QDir(dir.path()).filePath(QStringLiteral("cache.db")); }

}  // namespace

TEST_CASE("store and clear run in the order they were issued") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);

    // Both calls are issued before either is awaited. The single worker runs them in order.
    const auto report = make_report(QStringLiteral("author.example"), 0.5);
    auto stored = db.store_report(path, report);
    auto cleared = db.clear(path);
    REQUIRE(QCoro::waitFor(std::move(stored)).has_value());
    REQUIRE(QCoro::waitFor(std::move(cleared)).has_value());
    CHECK_FALSE(ok(db.get_latest_report(path)).has_value());

    ok(db.store_report(path, report));
    const auto latest = ok(db.get_latest_report(path));
    REQUIRE(latest.has_value());
    REQUIRE(latest->mods.size() == 1);
    CHECK(latest->mods[0].package_id == QStringLiteral("author.example"));
}

TEST_CASE("base game keeps its sentinel package id after a round trip") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);

    StartupImpactReport report;
    report.loading_time_s = 1.0;
    report.timestamp = QStringLiteral("now");
    report.mods.append(mod_entry(QStringLiteral("Example"), QStringLiteral("author.example"), 0.5));
    report.mods.append(mod_entry(QString(BASE_GAME_PID), QString(BASE_GAME_PID), 2.0));
    ok(db.store_report(path, report));
    const auto latest = ok(db.get_latest_report(path));
    REQUIRE(latest.has_value());

    QHash<QString, std::optional<QString>> by_name;
    for (const StartupImpactMod& mod : latest->mods) {
        by_name.insert(mod.mod_name, mod.package_id);
    }
    REQUIRE(by_name.value(QString(BASE_GAME_PID)) == QString(BASE_GAME_PID));
}

TEST_CASE("a mod stored without a package id reads back without one") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);

    StartupImpactReport report;
    report.loading_time_s = 1.0;
    report.mods.append(mod_entry(QStringLiteral("No Package"), std::nullopt, 0.2));
    ok(db.store_report(path, report));
    const auto latest = ok(db.get_latest_report(path));
    REQUIRE(latest.has_value());
    REQUIRE(latest->mods.size() == 1);
    CHECK_FALSE(latest->mods[0].package_id.has_value());
    CHECK(latest->mods[0].mod_name == QStringLiteral("No Package"));
}

TEST_CASE("metrics survive a round trip") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);

    StartupImpactReport report;
    report.loading_time_s = 3.0;
    StartupImpactMod harmony =
        mod_entry(QStringLiteral("Harmony"), QStringLiteral("brrainz.harmony"), 1.25, 0.3);
    harmony.metrics.insert(QStringLiteral("ModConstructor"), 1.0);
    harmony.off_thread_metrics.insert(QStringLiteral("LoadPatches"), 0.3);
    report.mods.append(std::move(harmony));
    ok(db.store_report(path, report));
    const auto latest = ok(db.get_latest_report(path));
    REQUIRE(latest.has_value());
    REQUIRE(latest->mods.size() == 1);
    const StartupImpactMod& mod = latest->mods[0];
    CHECK(mod.total_impact_s == Catch::Approx(1.25));
    CHECK(mod.metrics.value(QStringLiteral("ModConstructor")) == Catch::Approx(1.0));
    CHECK(mod.off_thread_metrics.value(QStringLiteral("LoadPatches")) == Catch::Approx(0.3));
    CHECK(mod.off_thread_total_impact_s == Catch::Approx(0.3));
    CHECK(latest->loading_time_s == Catch::Approx(3.0));
}

TEST_CASE("averages weight the newest session most") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);
    const QString pid = QStringLiteral("a.a");

    ok(db.store_report(path, make_report(pid, 1.0, 0.5)));
    ok(db.store_report(path, make_report(pid, 3.0, 1.5)));

    // Older session weighs 9, newest weighs 10: (1.0*9 + 3.0*10) / 19 and (0.5*9 + 1.5*10) / 19.
    CHECK_THAT(ok(db.get_average(path, pid)), WithinAbs(39.0 / 19.0, 1e-9));
    const auto totals = ok(db.get_totals_for(path, {pid}));
    REQUIRE(totals.contains(pid));
    CHECK_THAT(totals.value(pid).on_thread, WithinAbs(39.0 / 19.0, 1e-9));
    CHECK_THAT(totals.value(pid).off_thread, WithinAbs(19.5 / 19.0, 1e-9));
    CHECK(ok(db.get_all_averages(path)).value(pid) == Catch::Approx(39.0 / 19.0));
}

TEST_CASE("only the most recent sessions contribute to averages") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);

    ok(db.store_report(path, make_report(QStringLiteral("old.mod"), 100.0)));
    for (int i = 0; i < 5; ++i) {
        ok(db.store_report(path, make_report(QStringLiteral("new.mod"), 1.0)));
    }

    // Five sessions are kept, so the old one is gone from history entirely.
    CHECK(ok(db.get_average(path, QStringLiteral("old.mod"))) == 0.0);
    CHECK_THAT(ok(db.get_average(path, QStringLiteral("new.mod"))), WithinAbs(1.0, 1e-9));
    CHECK_FALSE(ok(db.get_all_averages(path)).contains(QStringLiteral("old.mod")));
}

TEST_CASE("snapshot returns the report with averages for the requested mods") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);

    StartupImpactReport report = make_report(QStringLiteral("a.a"), 1.0, 0.2);
    report.mods.append(mod_entry(QString(BASE_GAME_PID), QString(BASE_GAME_PID), 2.0));
    ok(db.store_report(path, report));

    const auto snap = ok(db.get_latest_with_averages(
        path, {QStringLiteral("a.a"), QStringLiteral("missing.mod")}, QStringLiteral("a.a")));
    REQUIRE(snap.report.has_value());
    CHECK(snap.report->mods.size() == 2);
    CHECK_THAT(snap.base_game_avg, WithinAbs(2.0, 1e-9));
    CHECK_THAT(snap.selected_avg, WithinAbs(1.0, 1e-9));
    REQUIRE(snap.totals.contains(QStringLiteral("a.a")));
    CHECK_THAT(snap.totals.value(QStringLiteral("a.a")).on_thread, WithinAbs(1.0, 1e-9));
    CHECK_THAT(snap.totals.value(QStringLiteral("a.a")).off_thread, WithinAbs(0.2, 1e-9));
    REQUIRE(snap.totals.contains(QStringLiteral("missing.mod")));
    CHECK(snap.totals.value(QStringLiteral("missing.mod")).on_thread == 0.0);
}

TEST_CASE("reads of a missing database return empty results and create no file") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);

    CHECK_FALSE(ok(db.get_latest_report(path)).has_value());
    CHECK(ok(db.get_average(path, QStringLiteral("a.a"))) == 0.0);
    CHECK(ok(db.get_all_averages(path)).isEmpty());

    const auto snap = ok(db.get_latest_with_averages(path, {QStringLiteral("a.a")}, {}));
    CHECK_FALSE(snap.report.has_value());
    CHECK(snap.totals.value(QStringLiteral("a.a")).on_thread == 0.0);
    CHECK_FALSE(QFileInfo::exists(path));
}

TEST_CASE("store creates a missing parent directory") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path =
        QDir(dir.path()).filePath(QStringLiteral("nested/config/cache.db"));

    ok(db.store_report(path, make_report(QStringLiteral("a.a"), 1.0)));
    CHECK(QFileInfo::exists(path));
}

TEST_CASE("clear removes all history") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);

    ok(db.store_report(path, make_report(QStringLiteral("a.a"), 1.0)));
    ok(db.clear(path));
    CHECK_FALSE(ok(db.get_latest_report(path)).has_value());
    CHECK(ok(db.get_all_averages(path)).isEmpty());
}

TEST_CASE("the database uses the legacy schema and version") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    StartupImpactDb db;
    const QString path = db_file(dir);
    ok(db.store_report(path, make_report(QStringLiteral("a.a"), 1.0)));

    // A second connection reads what the first wrote, as the legacy app would.
    const QString name = QStringLiteral("startup_impact_schema_check");
    {
        QSqlDatabase verify = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        verify.setDatabaseName(path);
        REQUIRE(verify.open());

        QSqlQuery version(verify);
        REQUIRE(version.exec(QStringLiteral("PRAGMA user_version")));
        REQUIRE(version.next());
        CHECK(version.value(0).toInt() == 1);

        QSqlQuery tables(verify);
        REQUIRE(tables.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type = 'table' AND name LIKE 'startup_impact_%'"
            " ORDER BY name")));
        QStringList names;
        while (tables.next()) {
            names.append(tables.value(0).toString());
        }
        CHECK(names == QStringList{QStringLiteral("startup_impact_entries"),
                                   QStringLiteral("startup_impact_sessions")});
    }
    QSqlDatabase::removeDatabase(name);
}
