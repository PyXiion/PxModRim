#include <QApplication>
#include <QEventLoop>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTimer>
#include <limits>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/context.hpp"
#include "test_support.hpp"
#include "ui/components/svg_provider.hpp"
#include "ui/panels/settings_panel.hpp"
#include "ui/theme/qml_theme.hpp"
#include "ui/ui_prefs.hpp"
namespace {
void ensureApplication() {
  if (QCoreApplication::instance()) return;
  static int argc = 1;
  static char name[] = "settings_panel_tests";
  static char* argv[] = {name, nullptr};
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  static QApplication app(argc, argv);
}
void settle(int milliseconds) {
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(milliseconds);
  loop.exec();
}
}  // namespace
TEST_CASE("Cancel settings unloads QML without binding errors") {
  ensureApplication();
  QWidget host;
  auto* engine = px::modrim::createQmlEngine(&host);
  engine->rootContext()->setContextProperty(QStringLiteral("Theme"),
                                            new px::modrim::Theme(engine));
  QList<QQmlError> warnings;
  QObject::connect(
      engine, &QQmlEngine::warnings, &host,
      [&](const QList<QQmlError>& values) { warnings.append(values); });
  auto context = px::ui_test::makeCore();
  px::modrim::SettingsPanel panel(*context, engine, &host);
  panel.resize(760, 640);
  panel.show();
  settle(300);
  auto* quick = panel.findChild<QQuickWidget*>();
  REQUIRE(quick != nullptr);
  auto* root = quick->rootObject();
  REQUIRE(root != nullptr);
  QQuickItem* cancel = nullptr;
  for (auto* item : root->findChildren<QQuickItem*>())
    if (QString::fromLatin1(item->metaObject()->className())
            .startsWith("PxButton") &&
        item->property("text").toString() == "Cancel") {
      cancel = item;
      break;
    }
  REQUIRE(cancel != nullptr);
  REQUIRE(QMetaObject::invokeMethod(cancel, "clicked", Qt::DirectConnection));
  settle(20);
  CHECK(quick->rootObject() == nullptr);
  CHECK_FALSE(panel.isVisible());
  CHECK(warnings.isEmpty());
}

TEST_CASE("Interface scale clamps JSON percentages and maps Qt factors") {
  using namespace px::modrim;
  const auto parse = [](std::string_view json) {
    glz::json_t value;
    REQUIRE_FALSE(glz::read_json(value, json));
    return parseUiScale(value);
  };

  CHECK(UiPrefs{}.ui_scale == 100);
  UiPrefs legacy;
  REQUIRE_FALSE(glz::read_json(legacy, R"({"rail_collapsed":true})"));
  CHECK(legacy.ui_scale == 100);
  CHECK(legacy.rail_collapsed);
  CHECK(clampUiScale(std::numeric_limits<int>::min()) == 100);
  CHECK(clampUiScale(137) == 137);
  CHECK(clampUiScale(std::numeric_limits<int>::max()) == 250);
  CHECK(parse("null") == 100);
  CHECK(parse("true") == 100);
  CHECK(parse(R"("garbage")") == 100);
  CHECK(parse(R"("150")") == 100);
  CHECK(parse("[]") == 100);
  CHECK(parse("{}") == 100);
  CHECK(parse("150.5") == 100);
  CHECK(parse("-1000") == 100);
  CHECK(parse("137") == 137);
  CHECK(parse("150") == 150);
  CHECK(parse("1000000000000") == 250);

  CHECK(uiScaleFactor(100) == "1");
  CHECK(uiScaleFactor(125) == "1.25");
  CHECK(uiScaleFactor(137) == "1.37");
  CHECK(uiScaleFactor(150) == "1.5");
  CHECK(uiScaleFactor(175) == "1.75");
  CHECK(uiScaleFactor(200) == "2");
  CHECK(uiScaleFactor(225) == "2.25");
  CHECK(uiScaleFactor(250) == "2.5");
}
