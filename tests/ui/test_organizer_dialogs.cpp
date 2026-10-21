#include <QMessageBox>
#include <QPushButton>
#include <QWidget>
#include <QtGlobal>

#include <catch2/catch_test_macros.hpp>

#include "ui/plugins/organizer/dialogs.hpp"

using px::modrim::DeleteFolderDialog;

TEST_CASE("delete folder dialog confirms with yes button") {
  QWidget parent;
  DeleteFolderDialog dialog(QStringLiteral("Fixes"), &parent);
  auto* yes = qobject_cast<QPushButton*>(dialog.button(QMessageBox::Yes));
  REQUIRE(yes != nullptr);
  CHECK(yes->text() == QStringLiteral("Delete"));
  yes->click();
  CHECK(dialog.result() == static_cast<int>(QMessageBox::Yes));
}
