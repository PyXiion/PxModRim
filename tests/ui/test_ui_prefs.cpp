#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "ui/config.hpp"
#include "ui/ui_prefs.hpp"

using namespace px::modrim;

TEST_CASE(
    "Startup interface scale resolves overrides without persisting them") {
  const UiPrefs prefs{.ui_scale = 125};
  const auto saved = resolveUiScale(prefs.ui_scale);
  CHECK(saved.scale == 125);
  CHECK(saved.source == "saved");

  const auto env = resolveUiScale(prefs.ui_scale, {}, "150");
  CHECK(env.scale == 150);
  CHECK(env.source == "env");

  const auto cli = resolveUiScale(prefs.ui_scale, "175", "150");
  CHECK(cli.scale == 175);
  CHECK(cli.source == "cli");

  const auto qt = resolveUiScale(prefs.ui_scale, "175", "150", "1.5");
  CHECK(qt.scale == 150);
  CHECK(qt.source == "qt-env");
  CHECK(qt.qtScaleFactor == "1.5");
  CHECK(resolveUiScale(125, {}, {}, "1.755").scale == 175.5);
  CHECK(resolveUiScale(125, "175", "150", "3").scale == 300);
  CHECK(resolveUiScale(125, "175", "150", QStringLiteral("")).source ==
        "qt-env");
  CHECK(resolveUiScale(125, "175", "150", "bad").scale == 100);
  CHECK(prefs.ui_scale == 125);

  const auto invalidCli = resolveUiScale(125, "bad", "150");
  CHECK(invalidCli.scale == 150);
  CHECK(invalidCli.source == "env");
  const auto invalidBoth = resolveUiScale(125, "1.5", "bad");
  CHECK(invalidBoth.scale == 125);
  CHECK(invalidBoth.source == "saved");
  CHECK(resolveUiScale(125, QStringLiteral(""), "150").source == "env");

  CHECK(resolveUiScale(50).scale == 100);
  CHECK(resolveUiScale(500).scale == 250);
  CHECK(resolveUiScale(125, "-50", "150").scale == 100);
  CHECK(resolveUiScale(125, "999", "150").scale == 250);
  CHECK(resolveUiScale(125, {}, "0").scale == 100);
  CHECK(resolveUiScale(125, {}, "1000000000000").scale == 250);
  CHECK(resolveUiScale(125, " 175 ").scale == 175);
  CHECK(resolveUiScale(125, "999999999999999999999999").scale == 250);
  CHECK(resolveUiScale(125, "-999999999999999999999999").scale == 100);
  CHECK(resolveUiScale(125, "+175").scale == 175);
  CHECK(resolveUiScale(125, "1_75", "150").source == "env");
}

TEST_CASE("Interface scale arguments leave unknown and Qt arguments alone") {
  CHECK(uiScaleArgument({"pxmodrim", "--ui-scale", "175"}) == "175");
  CHECK(uiScaleArgument({"pxmodrim", "--ui-scale=150"}) == "150");
  CHECK(uiScaleArgument({"pxmodrim", "-platform", "offscreen", "--unknown"})
            .isNull());
  CHECK(uiScaleArgument({"pxmodrim", "--", "--ui-scale=150"}).isNull());
  CHECK(uiScaleArgument({"pxmodrim", "--ui-scale=125", "--ui-scale", "175"}) ==
        "175");
  CHECK(uiScaleArgument({"pxmodrim", "--ui-scale"}).isEmpty());
  CHECK_FALSE(uiScaleArgument({"pxmodrim", "--ui-scale"}).isNull());
  CHECK(uiScaleArgument({"pxmodrim", "--ui-scale="}).isEmpty());
  CHECK_FALSE(uiScaleArgument({"pxmodrim", "--ui-scale="}).isNull());
  CHECK(resolveUiScale(200,
                       uiScaleArgument({"pxmodrim", "--ui-scale", "--unknown"}),
                       "150")
            .scale == 150);
}

TEST_CASE("An unusable saved interface scale keeps the other preferences") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const ConfigService config(directory.path());
  const std::pair<std::string, int> cases[] = {
      {R"("175")", 100},
      {"1.5", 100},
      {"150.5", 100},
      {"150.0", 150},
      {"1.5e2", 150},
      {R"("150.0")", 100},
      {"99999999999999999999999", 250},
      {"-99999999999999999999999", 100},
      {"null", 100},
      {"true", 100},
      {"{}", 100},
      {"175", 175},
      {"500", 250},
      {"50", 100},
  };
  for (const auto& [scale, expected] : cases) {
    CAPTURE(scale);
    const std::string text = R"({"rail_collapsed": true, )"
                             R"("skipped_update_tag": "v9.9.9", "ui_scale": )" +
                             scale + R"(, "schema_version": 1})";
    QFile file(QDir(directory.path()).filePath("ui_prefs.json"));
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text.data(), static_cast<qint64>(text.size()));
    file.close();

    const auto prefs = loadUiPrefs(config);
    REQUIRE(prefs);
    CHECK(prefs->rail_collapsed);
    CHECK(prefs->skipped_update_tag == "v9.9.9");
    CHECK(prefs->ui_scale == expected);
  }
}
