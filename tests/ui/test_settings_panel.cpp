#include <QEventLoop>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>

#include "core/context.hpp"
#include "test_support.hpp"
#include "ui/components/svg_provider.hpp"
#include "ui/panels/settings_panel.hpp"
#include "ui/theme/qml_theme.hpp"
namespace {
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
