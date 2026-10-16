#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QTemporaryDir>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "core/plugin_config.hpp"
#include "test_support.hpp"

namespace plugin_config_test {

using namespace px::modrim;
using test_support::readBytes;
using test_support::writeBytes;

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on the wire
struct DemoSettings {
  bool show_badges = false;
  int level = 0;

  bool operator==(const DemoSettings&) const = default;
};
// NOLINTEND(readability-identifier-naming)

}  // namespace plugin_config_test

using namespace plugin_config_test;

TEST_CASE(
    "plugin config seeds from legacy config.json keys and persists changes") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  REQUIRE(writeBytes(dir.filePath(QStringLiteral("config.json")),
                     R"({"old_badges":true,"paths":{}})"));
  const QString settingsPath =
      dir.filePath(QStringLiteral("plugins/demo.json"));

  ConfigService service(dir.path());
  PluginConfig<DemoSettings> settings(
      &service, QStringLiteral("demo"),
      QHash<QString, QString>{
          {QStringLiteral("old_badges"), QStringLiteral("show_badges")}});
  REQUIRE(settings.value().show_badges);
  REQUIRE(QFileInfo::exists(settingsPath));

  int emitted = 0;
  DemoSettings last;
  settings.changed.connect([&](const DemoSettings& value) {
    ++emitted;
    last = value;
  });
  settings.update(DemoSettings{.show_badges = false, .level = 2});
  REQUIRE(emitted == 1);
  REQUIRE(last == DemoSettings{.show_badges = false, .level = 2});

  settings.update(DemoSettings{.show_badges = false, .level = 2});
  REQUIRE(emitted == 1);

  PluginConfig<DemoSettings> reloaded(&service, QStringLiteral("demo"));
  REQUIRE(reloaded.value() == DemoSettings{.show_badges = false, .level = 2});
}

TEST_CASE("plugin config without a service keeps settings in memory") {
  PluginConfig<DemoSettings> settings(nullptr, QStringLiteral("memory"));
  REQUIRE(settings.value() == DemoSettings{});
  settings.update(DemoSettings{.show_badges = true, .level = 0});
  REQUIRE(settings.value().show_badges);
}
