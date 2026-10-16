#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QPushButton>

#include "ui/components/dialog_chrome.hpp"

namespace {

// The shared QApplication is created by tests/ui/main.cpp.
QApplication& application() { return *static_cast<QApplication*>(QApplication::instance()); }

void clickClose(QDialog& dialog) {
    px::modrim::installDialogChrome(&dialog);
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("dialogCloseBtn"));
    REQUIRE(button != nullptr);
    button->click();
}

}  // namespace

TEST_CASE("message box close yields cancel") {
    application();
    for (const auto buttons : {QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel}) {
        QMessageBox box;
        box.setStandardButtons(buttons);
        clickClose(box);
        CHECK(box.result() == static_cast<int>(QMessageBox::Cancel));
    }
}

TEST_CASE("plain dialog close rejects") {
    application();
    QDialog dialog;
    clickClose(dialog);
    CHECK(dialog.result() == QDialog::Rejected);
}
