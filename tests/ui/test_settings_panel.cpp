#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QEventLoop>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTimer>
#include "test_support.h"
#include "core/context.h"
#include "ui/components/svg_provider.h"
#include "ui/panels/settings_panel.h"
namespace {
void ensureApplication() {
    if (QCoreApplication::instance()) return;
    static int argc = 1;
    static char name[] = "settings_panel_tests";
    static char* argv[] = {name, nullptr};
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
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
}
TEST_CASE("Cancel settings unloads QML without binding errors") {
    ensureApplication();
    QWidget host;
    auto* engine = px::ui::createQmlEngine(&host);
    QList<QQmlError> warnings;
    QObject::connect(engine, &QQmlEngine::warnings, &host, [&](const QList<QQmlError>& values) { warnings.append(values); });
    auto context = px::ui_test::makeCore();
    px::ui::SettingsPanel panel(*context, engine, &host);
    panel.resize(760, 640);
    panel.show();
    settle(300);
    auto* quick = panel.findChild<QQuickWidget*>();
    REQUIRE(quick != nullptr);
    auto* root = quick->rootObject();
    REQUIRE(root != nullptr);
    QQuickItem* cancel = nullptr;
    for (auto* item : root->findChildren<QQuickItem*>())
        if (QString::fromLatin1(item->metaObject()->className()).startsWith("PxButton") && item->property("text").toString() == "Cancel") {
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
