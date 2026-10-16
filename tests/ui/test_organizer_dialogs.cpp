#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>
#include <QtGlobal>

#include "ui/plugins/organizer/dialogs.h"

using px::modrim::DeleteFolderDialog;

namespace {

// The shared QApplication is created by tests/ui/main.cpp.
QApplication& widget_app() { return *static_cast<QApplication*>(QApplication::instance()); }

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
