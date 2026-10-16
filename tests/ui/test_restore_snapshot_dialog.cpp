#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include "core/models/metadata/structures.hpp"
#include "core/mods_config.hpp"
#include "ui/panels/restore_snapshot_dialog.hpp"

namespace {

using px::modrim::CaseInsensitiveStr;
using px::modrim::ConfirmRestoreDialog;
using px::modrim::ModsConfig;
using px::modrim::RestoreSnapshotDialog;

// Widgets need a QApplication for the whole run; the first test that asks for
// it creates it.
QApplication& ensureApp() {
  if (auto* existing =
          qobject_cast<QApplication*>(QCoreApplication::instance()))
    return *existing;
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  static int argc = 1;
  static char name[] = "pxmodrim_tests";
  static char* argv[] = {name, nullptr};
  static QApplication app(argc, argv);
  return app;
}

void writeText(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  REQUIRE(file.write(bytes) == bytes.size());
}

void writeSnapshot(const QString& path, const QStringList& packageIds) {
  ModsConfig config;
  config.version = QStringLiteral("1.5");
  for (const QString& id : packageIds) {
    config.activeMods.append(CaseInsensitiveStr(id));
  }
  REQUIRE(
      px::modrim::writeModsConfig(path, config, QFileInfo(path).absolutePath())
          .has_value());
}

QListWidget* listOf(RestoreSnapshotDialog& dialog) {
  return dialog.findChild<QListWidget*>();
}

QPushButton* buttonLabelled(RestoreSnapshotDialog& dialog,
                            const QString& text) {
  for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
    if (button->text() == text) {
      return button;
    }
  }
  return nullptr;
}

// The details label is the only label carrying markup; the instruction line has
// none.
QString detailsText(RestoreSnapshotDialog& dialog) {
  for (QLabel* label : dialog.findChildren<QLabel*>()) {
    if (label->text().contains(QStringLiteral("<b>"))) {
      return label->text();
    }
  }
  return {};
}

}  // namespace

TEST_CASE(
    "Restore dialog selects readable snapshots and disables Restore for "
    "unreadable ones",
    "[ui][restore_snapshot]") {
  [[maybe_unused]] auto& app = ensureApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());

  const QString validSnap =
      dir.filePath(QStringLiteral("ModsConfig_20260922_120000.xml"));
  writeSnapshot(validSnap,
                {QStringLiteral("mod.foo"), QStringLiteral("mod.bar")});
  const QString corruptSnap =
      dir.filePath(QStringLiteral("ModsConfig_20260922_130000.xml"));
  writeText(corruptSnap, "<<<INVALID XML>>>");

  RestoreSnapshotDialog dialog(QStringList{validSnap, corruptSnap});
  QListWidget* list = listOf(dialog);
  REQUIRE(list != nullptr);
  REQUIRE(list->count() == 2);

  // The first readable snapshot is selected initially.
  REQUIRE(dialog.selectedSnapshot() == validSnap);
  QPushButton* restoreBtn = buttonLabelled(dialog, QStringLiteral("Restore"));
  QPushButton* cancelBtn = buttonLabelled(dialog, QStringLiteral("Cancel"));
  REQUIRE(restoreBtn != nullptr);
  REQUIRE(cancelBtn != nullptr);
  REQUIRE(restoreBtn->isEnabled());
  REQUIRE(restoreBtn->parentWidget() == &dialog);

  list->setCurrentRow(1);
  REQUIRE_FALSE(dialog.selectedSnapshot().has_value());
  REQUIRE_FALSE(restoreBtn->isEnabled());

  list->setCurrentRow(0);
  REQUIRE(dialog.selectedSnapshot() == validSnap);
  REQUIRE(restoreBtn->isEnabled());

  cancelBtn->click();
  REQUIRE(dialog.result() == QDialog::Rejected);

  RestoreSnapshotDialog acceptDialog(QStringList{validSnap});
  QListWidget* acceptList = listOf(acceptDialog);
  REQUIRE(acceptList != nullptr);
  emit acceptList->itemDoubleClicked(acceptList->item(0));
  REQUIRE(acceptDialog.result() == QDialog::Accepted);
  REQUIRE(acceptDialog.selectedSnapshot() == validSnap);
}

TEST_CASE("Restore dialog Restore button accepts the selected snapshot",
          "[ui][restore_snapshot]") {
  [[maybe_unused]] auto& app = ensureApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());

  const QString validSnap =
      dir.filePath(QStringLiteral("ModsConfig_20260922_120000.xml"));
  writeSnapshot(validSnap, {QStringLiteral("mod.foo")});

  RestoreSnapshotDialog dialog(QStringList{validSnap});
  QPushButton* restoreBtn = buttonLabelled(dialog, QStringLiteral("Restore"));
  REQUIRE(restoreBtn != nullptr);
  restoreBtn->click();
  REQUIRE(dialog.result() == QDialog::Accepted);
  REQUIRE(dialog.selectedSnapshot() == validSnap);
}

TEST_CASE(
    "Restore dialog double-click on an unreadable snapshot does not accept",
    "[ui][restore_snapshot]") {
  [[maybe_unused]] auto& app = ensureApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());

  const QString corruptSnap =
      dir.filePath(QStringLiteral("ModsConfig_20260922_130000.xml"));
  writeText(corruptSnap, "<<<INVALID XML>>>");

  RestoreSnapshotDialog dialog(QStringList{corruptSnap});
  QListWidget* list = listOf(dialog);
  REQUIRE(list != nullptr);
  emit list->itemDoubleClicked(list->item(0));
  REQUIRE(dialog.result() == QDialog::Rejected);
  REQUIRE_FALSE(dialog.selectedSnapshot().has_value());
}

TEST_CASE(
    "Restore dialog lists snapshot timestamps and mod counts in given order",
    "[ui][restore_snapshot]") {
  [[maybe_unused]] auto& app = ensureApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());

  const QString twoMods =
      dir.filePath(QStringLiteral("ModsConfig_20260922_120000.xml"));
  writeSnapshot(twoMods,
                {QStringLiteral("mod.foo"), QStringLiteral("mod.bar")});
  const QString oneMod =
      dir.filePath(QStringLiteral("ModsConfig_20260922_140000.xml"));
  writeSnapshot(oneMod, {QStringLiteral("mod.foo")});
  const QString noMods =
      dir.filePath(QStringLiteral("ModsConfig_20260922_150000_123456.xml"));
  writeSnapshot(noMods, {});
  const QString corrupt =
      dir.filePath(QStringLiteral("ModsConfig_20260922_130000.xml"));
  writeText(corrupt, "<<<INVALID XML>>>");

  RestoreSnapshotDialog dialog(QStringList{twoMods, oneMod, noMods, corrupt});
  QListWidget* list = listOf(dialog);
  REQUIRE(list != nullptr);
  REQUIRE(list->count() == 4);

  const QString dash = QString::fromUtf8("\u2014");
  REQUIRE(list->item(0)->text() ==
          QStringLiteral("2026-09-22 12:00:00 UTC ") + dash +
              QStringLiteral(" 2 active mods, RimWorld 1.5"));
  REQUIRE(list->item(1)->text() ==
          QStringLiteral("2026-09-22 14:00:00 UTC ") + dash +
              QStringLiteral(" 1 active mod, RimWorld 1.5"));
  REQUIRE(list->item(2)->text() ==
          QStringLiteral("2026-09-22 15:00:00 UTC ") + dash +
              QStringLiteral(" 0 active mods, RimWorld 1.5"));
  REQUIRE(list->item(3)->text() == QStringLiteral("2026-09-22 13:00:00 UTC ") +
                                       dash +
                                       QStringLiteral(" Cannot be read"));
  REQUIRE(list->item(0)->toolTip() ==
          QStringLiteral("ModsConfig_20260922_120000.xml"));
}

TEST_CASE(
    "Restore dialog falls back to file modification time for unnamed or "
    "invalid snapshot names",
    "[ui][restore_snapshot]") {
  [[maybe_unused]] auto& app = ensureApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());

  const QString unnamed = dir.filePath(QStringLiteral("custom.xml"));
  writeSnapshot(unnamed, {QStringLiteral("mod.foo")});
  const QString badDate =
      dir.filePath(QStringLiteral("ModsConfig_20261322_120000.xml"));
  writeSnapshot(badDate, {QStringLiteral("mod.foo")});

  RestoreSnapshotDialog dialog(QStringList{unnamed, badDate});
  QListWidget* list = listOf(dialog);
  REQUIRE(list != nullptr);
  REQUIRE(list->count() == 2);

  const QRegularExpression pattern(QStringLiteral(
      R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} UTC \x{2014} 1 active mod, RimWorld 1\.5$)"));
  REQUIRE(pattern.match(list->item(0)->text()).hasMatch());
  REQUIRE(pattern.match(list->item(1)->text()).hasMatch());
}

TEST_CASE("Restore dialog details describe the selected snapshot",
          "[ui][restore_snapshot]") {
  [[maybe_unused]] auto& app = ensureApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());

  const QString validSnap =
      dir.filePath(QStringLiteral("ModsConfig_20260922_120000.xml"));
  writeSnapshot(validSnap,
                {QStringLiteral("mod.foo"), QStringLiteral("mod.bar")});
  const QString corruptSnap =
      dir.filePath(QStringLiteral("ModsConfig_20260922_130000.xml"));
  writeText(corruptSnap, "<<<INVALID XML>>>");

  RestoreSnapshotDialog dialog(QStringList{validSnap, corruptSnap});
  QListWidget* list = listOf(dialog);
  REQUIRE(list != nullptr);

  REQUIRE(
      detailsText(dialog) ==
      QStringLiteral("<b>File:</b> ModsConfig_20260922_120000.xml<br><b>Active "
                     "mods:</b> 2 | "
                     "<b>Game version:</b> 1.5"));

  list->setCurrentRow(1);
  REQUIRE(detailsText(dialog) ==
          QStringLiteral("<b>This saved mod list cannot be read.</b> "
                         "ModsConfig_20260922_130000.xml "
                         "will not be restored."));

  list->setCurrentItem(nullptr);
  REQUIRE_FALSE(dialog.selectedSnapshot().has_value());
  REQUIRE(detailsText(dialog).isEmpty());
}

TEST_CASE("Restore dialog with no snapshots has nothing to select",
          "[ui][restore_snapshot]") {
  [[maybe_unused]] auto& app = ensureApp();

  RestoreSnapshotDialog dialog(QStringList{});
  QListWidget* list = listOf(dialog);
  REQUIRE(list != nullptr);
  REQUIRE(list->count() == 0);
  REQUIRE_FALSE(dialog.selectedSnapshot().has_value());
  QPushButton* restoreBtn = buttonLabelled(dialog, QStringLiteral("Restore"));
  REQUIRE(restoreBtn != nullptr);
  REQUIRE_FALSE(restoreBtn->isEnabled());
}

TEST_CASE("Confirm restore dialog names the snapshot and defaults to Cancel",
          "[ui][restore_snapshot]") {
  [[maybe_unused]] auto& app = ensureApp();

  ConfirmRestoreDialog confirm(
      QStringLiteral("ModsConfig_20260922_120000.xml"));
  REQUIRE(confirm.windowTitle() == QStringLiteral("Restore Mod List"));
  REQUIRE(confirm.text().contains(
      QStringLiteral("ModsConfig_20260922_120000.xml")));
  REQUIRE(confirm.informativeText().contains(QStringLiteral("backup")));
  REQUIRE(confirm.icon() == QMessageBox::Question);
  REQUIRE(confirm.defaultButton() == confirm.button(QMessageBox::Cancel));

  auto* restoreBtn =
      qobject_cast<QPushButton*>(confirm.button(QMessageBox::Yes));
  REQUIRE(restoreBtn != nullptr);
  REQUIRE(restoreBtn->text() == QStringLiteral("Restore"));
  REQUIRE(restoreBtn->objectName() == QStringLiteral("primaryAction"));
  auto* cancelBtn =
      qobject_cast<QPushButton*>(confirm.button(QMessageBox::Cancel));
  REQUIRE(cancelBtn != nullptr);
  REQUIRE(cancelBtn->text() == QStringLiteral("Cancel"));
}
