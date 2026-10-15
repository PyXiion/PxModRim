#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>
#include <QtGlobal>

#include "ui/plugins/organizer/dialogs.h"

using px::ui::DeleteFolderDialog;

namespace {

// Widgets need a QApplication. The test binary keeps one for its whole run, offscreen
// unless the environment picks a platform, so no display is needed.
QApplication& widget_app() {
    static int argc = 1;
    static char name[] = "pxmodrim_ui_tests";
    static char* argv[] = {name, nullptr};
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    static QApplication app(argc, argv);
    return app;
}

}  // namespace

TEST_CASE("delete folder dialog confirms with yes button") {
    widget_app();
    QWidget parent;
    DeleteFolderDialog dialog(QStringLiteral("Fixes"), &parent);
    auto* yes = qobject_cast<QPushButton*>(dialog.button(QMessageBox::Yes));
    REQUIRE(yes != nullptr);
    CHECK(yes->text() == QStringLiteral("Delete"));
    yes->click();
    CHECK(dialog.result() == static_cast<int>(QMessageBox::Yes));
}
