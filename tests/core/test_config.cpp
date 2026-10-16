#include <catch2/catch_test_macros.hpp>

#include <string>

#include <glaze/glaze.hpp>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include "core/config.h"
#include "core/constants.h"
#include "core/file_io.h"
#include "test_support.h"

namespace {

using namespace px::modrim;
using test_support::read_bytes;
using test_support::run_sync;
using test_support::write_bytes;

// Stands in for ui_prefs.json's struct: these tests exercise ConfigService's
// versioning, not the UI settings themselves.
struct TestPrefs {
    bool desc_expanded = false;
    LaunchStrategy launch_strategy = LaunchStrategy::Direct;
    std::string skipped_update_tag;

    bool operator==(const TestPrefs&) const = default;
};

QString path_in(const QTemporaryDir& dir, const char* name) { return dir.filePath(QString::fromLatin1(name)); }

QStringList backups_of(const QTemporaryDir& dir, const char* name) {
    return QDir(dir.path()).entryList({QString::fromLatin1(name) + QStringLiteral(".bak.*")}, QDir::Files);
}

int schema_version_of(const QString& path) {
    glz::json_t document;
    REQUIRE_FALSE(glz::read_json(document, read_bytes(path)));
    const auto& object = document.get_object();
    const auto marker = object.find(std::string(kSchemaVersionKey));
    REQUIRE(marker != object.end());
    return static_cast<int>(marker->second.get<double>());
}

AppConfig legacy_expected_config() {
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
    REQUIRE(write_bytes(path_in(dir, "Version.txt"), "1.6.4871 rev598\n"));
    REQUIRE(read_game_version(dir.path()) == QStringLiteral("1.6.4871 rev598"));

    REQUIRE(write_bytes(path_in(dir, "Version.txt"), "  1.6.4871 rev598  \n"));
    REQUIRE(read_game_version(dir.path()) == QStringLiteral("1.6.4871 rev598"));
}

TEST_CASE("read_game_version returns nullopt when Version.txt is missing or unreadable") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    REQUIRE_FALSE(read_game_version(dir.path()).has_value());

    REQUIRE(write_bytes(path_in(dir, "Version.txt"), "1.6.4871 rev598\n"));
    REQUIRE(QFile(path_in(dir, "Version.txt")).setPermissions(QFileDevice::Permissions{}));
    REQUIRE_FALSE(read_game_version(dir.path()).has_value());
}

TEST_CASE("legacy app config migrates with a backup") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = path_in(dir, "config.json");
    const std::string legacy =
        R"({"paths":{"game":"/legacy/game","local":"/legacy/mods","workshop":"/legacy/workshop",)"
        R"("config_folder":"/legacy/config","community_rules_file":"/legacy/rules",)"
        R"("no_version_warning_file":"/legacy/no-warning","use_this_instead_file":"/legacy/use-instead",)"
        R"("steamcmd_prefix":"/legacy/steamcmd"}})";
    REQUIRE(write_bytes(path, legacy));

    ConfigService service(dir.path());
    AppConfigService app(service);
    REQUIRE(run_sync(app.setup()).has_value());

    REQUIRE(app.cfg() == legacy_expected_config());
    REQUIRE(schema_version_of(path) == 1);
    const auto backups = backups_of(dir, "config.json");
    REQUIRE(backups.size() == 1);
    REQUIRE(read_bytes(dir.filePath(backups.first())) == legacy);
}

TEST_CASE("legacy ui prefs migrate with a backup") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = path_in(dir, "ui_prefs.json");
    const std::string legacy = R"({"validate_downloads":true,"desc_expanded":true,"launch_strategy":1})";
    REQUIRE(write_bytes(path, legacy));

    ConfigService service(dir.path());
    const auto prefs = service.load<TestPrefs>("ui_prefs.json");
    REQUIRE(prefs.has_value());
    REQUIRE(*prefs == TestPrefs{.desc_expanded = true, .launch_strategy = LaunchStrategy::Steam, .skipped_update_tag = ""});
    REQUIRE(schema_version_of(path) == 1);
    const auto backups = backups_of(dir, "ui_prefs.json");
    REQUIRE(backups.size() == 1);
    REQUIRE(read_bytes(dir.filePath(backups.first())) == legacy);
}

TEST_CASE("fresh services return defaults without creating files") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    ConfigService service(dir.path());
    AppConfigService app(service);
    REQUIRE(run_sync(app.setup()).has_value());

    REQUIRE(app.cfg() == AppConfig{});
    REQUIRE(service.load<TestPrefs>("ui_prefs.json") == TestPrefs{});
    REQUIRE_FALSE(QFileInfo::exists(path_in(dir, "config.json")));
    REQUIRE_FALSE(QFileInfo::exists(path_in(dir, "ui_prefs.json")));
}

TEST_CASE("managed saves stamp schema_version and round-trip values") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    ConfigService service(dir.path());
    AppConfig expected_config;
    expected_config.paths.game = "/game";
    expected_config.paths.local = "/mods";
    const TestPrefs expected_prefs{.desc_expanded = true, .launch_strategy = LaunchStrategy::Steam, .skipped_update_tag = ""};

    REQUIRE(service.save("config.json", expected_config).has_value());
    REQUIRE(read_bytes(path_in(dir, "config.json")).find("\"schema_version\": 1\n}") != std::string::npos);
    REQUIRE(service.save("ui_prefs.json", expected_prefs).has_value());

    REQUIRE(schema_version_of(path_in(dir, "config.json")) == 1);
    REQUIRE(schema_version_of(path_in(dir, "ui_prefs.json")) == 1);
    REQUIRE(service.load<AppConfig>("config.json") == expected_config);
    REQUIRE(service.load<TestPrefs>("ui_prefs.json") == expected_prefs);
}

TEST_CASE("current config load does not rewrite the file") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = path_in(dir, "config.json");
    REQUIRE(write_bytes(path, R"({"schema_version":1,"paths":{"game":"/current"}})"));
    const std::string before = read_bytes(path);

    const auto cfg = ConfigService(dir.path()).load<AppConfig>("config.json");
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->paths.game == "/current");
    REQUIRE(read_bytes(path) == before);
    REQUIRE(backups_of(dir, "config.json").isEmpty());
}

TEST_CASE("current ui prefs load does not rewrite the file") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = path_in(dir, "ui_prefs.json");
    REQUIRE(write_bytes(path, R"({"schema_version":1,"desc_expanded":true})"));
    const std::string before = read_bytes(path);

    const auto prefs = ConfigService(dir.path()).load<TestPrefs>("ui_prefs.json");
    REQUIRE(prefs.has_value());
    REQUIRE(prefs->desc_expanded);
    REQUIRE(read_bytes(path) == before);
    REQUIRE(backups_of(dir, "ui_prefs.json").isEmpty());
}

TEST_CASE("corrupt config returns safe defaults") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    REQUIRE(write_bytes(path_in(dir, "config.json"), "{invalid"));
    REQUIRE(ConfigService(dir.path()).load<AppConfig>("config.json") == AppConfig{});
}

TEST_CASE("unreadable config returns safe defaults") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = path_in(dir, "config.json");
    REQUIRE(write_bytes(path, "{}"));
    REQUIRE(QFile(path).setPermissions(QFileDevice::Permissions{}));
    REQUIRE(ConfigService(dir.path()).load<AppConfig>("config.json") == AppConfig{});
}

TEST_CASE("missing JSON migration step fails without rewrite") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = path_in(dir, "config.json");
    REQUIRE(write_bytes(path, R"({"paths":{"game":"/legacy"}})"));
    const std::string before = read_bytes(path);

    const ConfigService service(dir.path(), JsonMigrations{});
    const auto loaded = service.load<AppConfig>("config.json");
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(loaded.error().message.contains("Missing JSON migration step"));
    REQUIRE(read_bytes(path) == before);
    REQUIRE(backups_of(dir, "config.json").isEmpty());
}

TEST_CASE("unsupported config schema fails without mutation") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = path_in(dir, "config.json");
    REQUIRE(write_bytes(path, R"({"schema_version":2})"));
    const std::string before = read_bytes(path);

    const auto loaded = ConfigService(dir.path()).load<AppConfig>("config.json");
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(loaded.error().message.contains("Unsupported JSON schema version"));
    REQUIRE(read_bytes(path) == before);
}

TEST_CASE("ui prefs skipped update tag round-trips") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    ConfigService service(dir.path());
    REQUIRE(service.save("ui_prefs.json", TestPrefs{.desc_expanded = false, .launch_strategy = LaunchStrategy::Direct, .skipped_update_tag = "v0.2.0"}).has_value());
    REQUIRE(service.load<TestPrefs>("ui_prefs.json").value().skipped_update_tag == "v0.2.0");
}

TEST_CASE("ui prefs with removed keys still load") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    REQUIRE(write_bytes(path_in(dir, "ui_prefs.json"),
                        R"({"schema_version":1,"deps_expanded":true,"desc_expanded":true,)"
                        R"("skipped_update_tag":"v0.3.0"})"));

    const auto prefs = ConfigService(dir.path()).load<TestPrefs>("ui_prefs.json");
    REQUIRE(prefs.has_value());
    REQUIRE(prefs->desc_expanded);
    REQUIRE(prefs->skipped_update_tag == "v0.3.0");
}
