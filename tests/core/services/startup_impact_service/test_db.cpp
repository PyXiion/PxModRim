#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <type_traits>

#include <QCoro/QCoroTask>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

StartupImpactMod modEntry(const QString& name,
                          const std::optional<QString>& pid, double totalS,
                          double offS = 0.0) {
  StartupImpactMod mod;
  mod.modName = name;
  mod.packageId = pid;
  mod.totalImpactS = totalS;
  mod.offThreadTotalImpactS = offS;
  return mod;
}

StartupImpactReport makeReport(const QString& pid, double totalS,
                               double offThreadS = 0.0) {
  StartupImpactReport report;
  report.loadingTimeS = 1.0;
  report.timestamp = QStringLiteral("now");
  report.mods.append(
      modEntry(QStringLiteral("Mod ") + pid, pid, totalS, offThreadS));
  return report;
}

QString dbFile(const QTemporaryDir& dir) {
  return QDir(dir.path()).filePath(QStringLiteral("cache.db"));
}

}  // namespace

TEST_CASE("store and clear run in the order they were issued") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);

  // Both calls are issued before either is awaited. The single worker runs them
  // in order.
  const auto report = makeReport(QStringLiteral("author.example"), 0.5);
  auto stored = db.storeReport(path, report);
  auto cleared = db.clear(path);
  REQUIRE(QCoro::waitFor(std::move(stored)).has_value());
  REQUIRE(QCoro::waitFor(std::move(cleared)).has_value());
  CHECK_FALSE(ok(db.getLatestReport(path)).has_value());

  ok(db.storeReport(path, report));
  const auto latest = ok(db.getLatestReport(path));
  REQUIRE(latest.has_value());
  REQUIRE(latest->mods.size() == 1);
  CHECK(latest->mods[0].packageId == QStringLiteral("author.example"));
}

TEST_CASE("base game keeps its sentinel package id after a round trip") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);

  StartupImpactReport report;
  report.loadingTimeS = 1.0;
  report.timestamp = QStringLiteral("now");
  report.mods.append(modEntry(QStringLiteral("Example"),
                              QStringLiteral("author.example"), 0.5));
  report.mods.append(
      modEntry(QString(kBaseGamePid), QString(kBaseGamePid), 2.0));
  ok(db.storeReport(path, report));
  const auto latest = ok(db.getLatestReport(path));
  REQUIRE(latest.has_value());

  QHash<QString, std::optional<QString>> byName;
  for (const StartupImpactMod& mod : latest->mods) {
    byName.insert(mod.modName, mod.packageId);
  }
  REQUIRE(byName.value(QString(kBaseGamePid)) == QString(kBaseGamePid));
}

TEST_CASE("a mod stored without a package id reads back without one") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);

  StartupImpactReport report;
  report.loadingTimeS = 1.0;
  report.mods.append(modEntry(QStringLiteral("No Package"), std::nullopt, 0.2));
  ok(db.storeReport(path, report));
  const auto latest = ok(db.getLatestReport(path));
  REQUIRE(latest.has_value());
  REQUIRE(latest->mods.size() == 1);
  CHECK_FALSE(latest->mods[0].packageId.has_value());
  CHECK(latest->mods[0].modName == QStringLiteral("No Package"));
}

TEST_CASE("metrics survive a round trip") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);

  StartupImpactReport report;
  report.loadingTimeS = 3.0;
  StartupImpactMod harmony = modEntry(
      QStringLiteral("Harmony"), QStringLiteral("brrainz.harmony"), 1.25, 0.3);
  harmony.metrics.insert(QStringLiteral("ModConstructor"), 1.0);
  harmony.offThreadMetrics.insert(QStringLiteral("LoadPatches"), 0.3);
  report.mods.append(std::move(harmony));
  ok(db.storeReport(path, report));
  const auto latest = ok(db.getLatestReport(path));
  REQUIRE(latest.has_value());
  REQUIRE(latest->mods.size() == 1);
  const StartupImpactMod& mod = latest->mods[0];
  CHECK(mod.totalImpactS == Catch::Approx(1.25));
  CHECK(mod.metrics.value(QStringLiteral("ModConstructor")) ==
        Catch::Approx(1.0));
  CHECK(mod.offThreadMetrics.value(QStringLiteral("LoadPatches")) ==
        Catch::Approx(0.3));
  CHECK(mod.offThreadTotalImpactS == Catch::Approx(0.3));
  CHECK(latest->loadingTimeS == Catch::Approx(3.0));
}

TEST_CASE("averages weight the newest session most") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);
  const QString pid = QStringLiteral("a.a");

  ok(db.storeReport(path, makeReport(pid, 1.0, 0.5)));
  ok(db.storeReport(path, makeReport(pid, 3.0, 1.5)));

  // Older session weighs 9, newest weighs 10: (1.0*9 + 3.0*10) / 19 and (0.5*9
  // + 1.5*10) / 19.
  const auto snap = ok(db.getLatestWithAverages(path, {pid}, pid));
  CHECK_THAT(snap.selectedAvg, WithinAbs(39.0 / 19.0, 1e-9));
  REQUIRE(snap.totals.contains(pid));
  CHECK_THAT(snap.totals.value(pid).onThread, WithinAbs(39.0 / 19.0, 1e-9));
  CHECK_THAT(snap.totals.value(pid).offThread, WithinAbs(19.5 / 19.0, 1e-9));
  CHECK(ok(db.getAllAverages(path)).value(pid) == Catch::Approx(39.0 / 19.0));
}

TEST_CASE("only the most recent sessions contribute to averages") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);

  ok(db.storeReport(path, makeReport(QStringLiteral("old.mod"), 100.0)));
  for (int i = 0; i < 5; ++i) {
    ok(db.storeReport(path, makeReport(QStringLiteral("new.mod"), 1.0)));
  }

  // Five sessions are kept, so the old one is gone from history entirely.
  const auto snap = ok(db.getLatestWithAverages(
      path, {QStringLiteral("old.mod"), QStringLiteral("new.mod")}, {}));
  CHECK(snap.totals.value(QStringLiteral("old.mod")).onThread == 0.0);
  CHECK_THAT(snap.totals.value(QStringLiteral("new.mod")).onThread,
             WithinAbs(1.0, 1e-9));
  CHECK_FALSE(ok(db.getAllAverages(path)).contains(QStringLiteral("old.mod")));
}

TEST_CASE("snapshot returns the report with averages for the requested mods") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);

  StartupImpactReport report = makeReport(QStringLiteral("a.a"), 1.0, 0.2);
  report.mods.append(
      modEntry(QString(kBaseGamePid), QString(kBaseGamePid), 2.0));
  ok(db.storeReport(path, report));

  const auto snap = ok(db.getLatestWithAverages(
      path, {QStringLiteral("a.a"), QStringLiteral("missing.mod")},
      QStringLiteral("a.a")));
  REQUIRE(snap.report.has_value());
  CHECK(snap.report->mods.size() == 2);
  CHECK_THAT(snap.baseGameAvg, WithinAbs(2.0, 1e-9));
  CHECK_THAT(snap.selectedAvg, WithinAbs(1.0, 1e-9));
  REQUIRE(snap.totals.contains(QStringLiteral("a.a")));
  CHECK_THAT(snap.totals.value(QStringLiteral("a.a")).onThread,
             WithinAbs(1.0, 1e-9));
  CHECK_THAT(snap.totals.value(QStringLiteral("a.a")).offThread,
             WithinAbs(0.2, 1e-9));
  REQUIRE(snap.totals.contains(QStringLiteral("missing.mod")));
  CHECK(snap.totals.value(QStringLiteral("missing.mod")).onThread == 0.0);
}

TEST_CASE(
    "reads of a missing database return empty results and create no file") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);

  CHECK_FALSE(ok(db.getLatestReport(path)).has_value());
  CHECK(ok(db.getAllAverages(path)).isEmpty());

  const auto snap =
      ok(db.getLatestWithAverages(path, {QStringLiteral("a.a")}, {}));
  CHECK_FALSE(snap.report.has_value());
  CHECK(snap.totals.value(QStringLiteral("a.a")).onThread == 0.0);
  CHECK_FALSE(QFileInfo::exists(path));
}

TEST_CASE("store creates a missing parent directory") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path =
      QDir(dir.path()).filePath(QStringLiteral("nested/config/cache.db"));

  ok(db.storeReport(path, makeReport(QStringLiteral("a.a"), 1.0)));
  CHECK(QFileInfo::exists(path));
}

TEST_CASE("clear removes all history") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);

  ok(db.storeReport(path, makeReport(QStringLiteral("a.a"), 1.0)));
  ok(db.clear(path));
  CHECK_FALSE(ok(db.getLatestReport(path)).has_value());
  CHECK(ok(db.getAllAverages(path)).isEmpty());
}

TEST_CASE("the database uses the legacy schema and version") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  StartupImpactDb db;
  const QString path = dbFile(dir);
  ok(db.storeReport(path, makeReport(QStringLiteral("a.a"), 1.0)));

  // A second connection reads what the first wrote, as the legacy app would.
  const QString name = QStringLiteral("startup_impact_schema_check");
  {
    QSqlDatabase verify =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    verify.setDatabaseName(path);
    REQUIRE(verify.open());

    QSqlQuery version(verify);
    REQUIRE(version.exec(QStringLiteral("PRAGMA user_version")));
    REQUIRE(version.next());
    CHECK(version.value(0).toInt() == 1);

    QSqlQuery tables(verify);
    REQUIRE(
        tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type "
                                   "= 'table' AND name LIKE 'startup_impact_%'"
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
