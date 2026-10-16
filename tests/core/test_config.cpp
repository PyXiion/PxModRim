#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <glaze/glaze.hpp>

#include "core/config.hpp"
#include "core/constants.hpp"
#include "core/file_io.hpp"
#include "test_support.hpp"

namespace {

using namespace px::modrim;
using test_support::readBytes;
using test_support::runSync;
using test_support::writeBytes;

// Stands in for ui_prefs.json's struct: these tests exercise ConfigService's
// versioning, not the UI settings themselves.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct TestPrefs {
  bool desc_expanded = false;
  LaunchStrategy launch_strategy = LaunchStrategy::kDirect;
  std::string skipped_update_tag;

  bool operator==(const TestPrefs&) const = default;
};
// NOLINTEND(readability-identifier-naming)

QString pathIn(const QTemporaryDir& dir, const char* name) {
  return dir.filePath(QString::fromLatin1(name));
}

QStringList backupsOf(const QTemporaryDir& dir, const char* name) {
  return QDir(dir.path())
      .entryList({QString::fromLatin1(name) + QStringLiteral(".bak.*")},
                 QDir::Files);
}

int schemaVersionOf(const QString& path) {
  glz::json_t document;
  REQUIRE_FALSE(glz::read_json(document, readBytes(path)));
  const auto& object = document.get_object();
  const auto marker = object.find(std::string(kSchemaVersionKey));
  REQUIRE(marker != object.end());
  return static_cast<int>(marker->second.get<double>());
}

AppConfig legacyExpectedConfig() {
  AppConfig cfg;
  cfg.paths = PathConfig{
      .game = "/legacy/game",
      .local = "/legacy/mods",
      .workshop = "/legacy/workshop",
      .config_folder = "/legacy/config",
      .community_rules_file = "/legacy/rules",
      .no_version_warning_file = "/legacy/no-warning",
      .use_this_instead_file = "/legacy/use-instead",
  };
  return cfg;
}

}  // namespace

TEST_CASE("read_game_version reads and trims Version.txt") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  REQUIRE(writeBytes(pathIn(dir, "Version.txt"), "1.6.4871 rev598\n"));
  REQUIRE(readGameVersion(dir.path()) == QStringLiteral("1.6.4871 rev598"));

  REQUIRE(writeBytes(pathIn(dir, "Version.txt"), "  1.6.4871 rev598  \n"));
  REQUIRE(readGameVersion(dir.path()) == QStringLiteral("1.6.4871 rev598"));
}

TEST_CASE(
    "read_game_version returns nullopt when Version.txt is missing or "
    "unreadable") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  REQUIRE_FALSE(readGameVersion(dir.path()).has_value());

  REQUIRE(writeBytes(pathIn(dir, "Version.txt"), "1.6.4871 rev598\n"));
  REQUIRE(QFile(pathIn(dir, "Version.txt"))
              .setPermissions(QFileDevice::Permissions{}));
  REQUIRE_FALSE(readGameVersion(dir.path()).has_value());
}

TEST_CASE("legacy app config migrates with a backup") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = pathIn(dir, "config.json");
  const std::string legacy =
      R"({"paths":{"game":"/legacy/game","local":"/legacy/mods","workshop":"/legacy/workshop",)"
      R"("config_folder":"/legacy/config","community_rules_file":"/legacy/rules",)"
      R"("no_version_warning_file":"/legacy/no-warning","use_this_instead_file":"/legacy/use-instead",)"
      R"("steamcmd_prefix":"/legacy/steamcmd"}})";
  REQUIRE(writeBytes(path, legacy));

  ConfigService service(dir.path());
  AppConfigService app(service);
  REQUIRE(runSync(app.setup()).has_value());

  REQUIRE(app.cfg() == legacyExpectedConfig());
  REQUIRE(schemaVersionOf(path) == 1);
  const auto backups = backupsOf(dir, "config.json");
  REQUIRE(backups.size() == 1);
  REQUIRE(readBytes(dir.filePath(backups.first())) == legacy);
}

TEST_CASE("legacy ui prefs migrate with a backup") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = pathIn(dir, "ui_prefs.json");
  const std::string legacy =
      R"({"validate_downloads":true,"desc_expanded":true,"launch_strategy":1})";
  REQUIRE(writeBytes(path, legacy));

  ConfigService service(dir.path());
  const auto prefs = service.load<TestPrefs>("ui_prefs.json");
  REQUIRE(prefs.has_value());
  REQUIRE(*prefs == TestPrefs{.desc_expanded = true,
                              .launch_strategy = LaunchStrategy::kSteam,
                              .skipped_update_tag = ""});
  REQUIRE(schemaVersionOf(path) == 1);
  const auto backups = backupsOf(dir, "ui_prefs.json");
  REQUIRE(backups.size() == 1);
  REQUIRE(readBytes(dir.filePath(backups.first())) == legacy);
}

TEST_CASE("fresh services return defaults without creating files") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  ConfigService service(dir.path());
  AppConfigService app(service);
  REQUIRE(runSync(app.setup()).has_value());

  REQUIRE(app.cfg() == AppConfig{});
  REQUIRE(service.load<TestPrefs>("ui_prefs.json") == TestPrefs{});
  REQUIRE_FALSE(QFileInfo::exists(pathIn(dir, "config.json")));
  REQUIRE_FALSE(QFileInfo::exists(pathIn(dir, "ui_prefs.json")));
}

TEST_CASE("managed saves stamp schema_version and round-trip values") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  ConfigService service(dir.path());
  AppConfig expectedConfig;
  expectedConfig.paths.game = "/game";
  expectedConfig.paths.local = "/mods";
  const TestPrefs expectedPrefs{.desc_expanded = true,
                                .launch_strategy = LaunchStrategy::kSteam,
                                .skipped_update_tag = ""};

  REQUIRE(service.save("config.json", expectedConfig).has_value());
  REQUIRE(
      readBytes(pathIn(dir, "config.json")).find("\"schema_version\": 1\n}") !=
      std::string::npos);
  REQUIRE(service.save("ui_prefs.json", expectedPrefs).has_value());

  REQUIRE(schemaVersionOf(pathIn(dir, "config.json")) == 1);
  REQUIRE(schemaVersionOf(pathIn(dir, "ui_prefs.json")) == 1);
  REQUIRE(service.load<AppConfig>("config.json") == expectedConfig);
  REQUIRE(service.load<TestPrefs>("ui_prefs.json") == expectedPrefs);
}

TEST_CASE("current config load does not rewrite the file") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = pathIn(dir, "config.json");
  REQUIRE(
      writeBytes(path, R"({"schema_version":1,"paths":{"game":"/current"}})"));
  const std::string before = readBytes(path);

  const auto cfg = ConfigService(dir.path()).load<AppConfig>("config.json");
  REQUIRE(cfg.has_value());
  REQUIRE(cfg->paths.game == "/current");
  REQUIRE(readBytes(path) == before);
  REQUIRE(backupsOf(dir, "config.json").isEmpty());
}

TEST_CASE("current ui prefs load does not rewrite the file") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = pathIn(dir, "ui_prefs.json");
  REQUIRE(writeBytes(path, R"({"schema_version":1,"desc_expanded":true})"));
  const std::string before = readBytes(path);

  const auto prefs = ConfigService(dir.path()).load<TestPrefs>("ui_prefs.json");
  REQUIRE(prefs.has_value());
  REQUIRE(prefs->desc_expanded);
  REQUIRE(readBytes(path) == before);
  REQUIRE(backupsOf(dir, "ui_prefs.json").isEmpty());
}

TEST_CASE("corrupt config returns safe defaults") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  REQUIRE(writeBytes(pathIn(dir, "config.json"), "{invalid"));
  REQUIRE(ConfigService(dir.path()).load<AppConfig>("config.json") ==
          AppConfig{});
}

TEST_CASE("unreadable config returns safe defaults") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = pathIn(dir, "config.json");
  REQUIRE(writeBytes(path, "{}"));
  REQUIRE(QFile(path).setPermissions(QFileDevice::Permissions{}));
  REQUIRE(ConfigService(dir.path()).load<AppConfig>("config.json") ==
          AppConfig{});
}

TEST_CASE("missing JSON migration step fails without rewrite") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = pathIn(dir, "config.json");
  REQUIRE(writeBytes(path, R"({"paths":{"game":"/legacy"}})"));
  const std::string before = readBytes(path);

  const ConfigService service(dir.path(), JsonMigrations{});
  const auto loaded = service.load<AppConfig>("config.json");
  REQUIRE_FALSE(loaded.has_value());
  REQUIRE(loaded.error().message.contains("Missing JSON migration step"));
  REQUIRE(readBytes(path) == before);
  REQUIRE(backupsOf(dir, "config.json").isEmpty());
}

TEST_CASE("unsupported config schema fails without mutation") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = pathIn(dir, "config.json");
  REQUIRE(writeBytes(path, R"({"schema_version":2})"));
  const std::string before = readBytes(path);

  const auto loaded = ConfigService(dir.path()).load<AppConfig>("config.json");
  REQUIRE_FALSE(loaded.has_value());
  REQUIRE(loaded.error().message.contains("Unsupported JSON schema version"));
  REQUIRE(readBytes(path) == before);
}

TEST_CASE("ui prefs skipped update tag round-trips") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  ConfigService service(dir.path());
  REQUIRE(service
              .save("ui_prefs.json",
                    TestPrefs{.desc_expanded = false,
                              .launch_strategy = LaunchStrategy::kDirect,
                              .skipped_update_tag = "v0.2.0"})
              .has_value());
  REQUIRE(service.load<TestPrefs>("ui_prefs.json").value().skipped_update_tag ==
          "v0.2.0");
}

TEST_CASE("ui prefs with removed keys still load") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  REQUIRE(writeBytes(
      pathIn(dir, "ui_prefs.json"),
      R"({"schema_version":1,"deps_expanded":true,"desc_expanded":true,)"
      R"("skipped_update_tag":"v0.3.0"})"));

  const auto prefs = ConfigService(dir.path()).load<TestPrefs>("ui_prefs.json");
  REQUIRE(prefs.has_value());
  REQUIRE(prefs->desc_expanded);
  REQUIRE(prefs->skipped_update_tag == "v0.3.0");
}
