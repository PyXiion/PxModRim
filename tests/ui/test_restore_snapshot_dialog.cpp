#include <catch2/catch_test_macros.hpp>

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

#include "core/mods_config.h"
#include "core/models/metadata/structures.h"
#include "ui/panels/restore_snapshot_dialog.h"

namespace {

using px::modrim::CaseInsensitiveStr;
using px::modrim::ModsConfig;
using px::modrim::ConfirmRestoreDialog;
using px::modrim::RestoreSnapshotDialog;

// Widgets need a QApplication for the whole run; the first test that asks for it creates it.
QApplication& ensure_app() {
    if (auto* existing = qobject_cast<QApplication*>(QCoreApplication::instance())) return *existing;
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    static int argc = 1;
    static char name[] = "pxmodrim_tests";
    static char* argv[] = {name, nullptr};
    static QApplication app(argc, argv);
    return app;
}

void write_text(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(bytes) == bytes.size());
}

void write_snapshot(const QString& path, const QStringList& package_ids) {
    ModsConfig config;
    config.version = QStringLiteral("1.5");
    for (const QString& id : package_ids) {
        config.activeMods.append(CaseInsensitiveStr(id));
    }
    REQUIRE(px::modrim::write_mods_config(path, config, QFileInfo(path).absolutePath()).has_value());
}

QListWidget* list_of(RestoreSnapshotDialog& dialog) {
    return dialog.findChild<QListWidget*>();
}

QPushButton* button_labelled(RestoreSnapshotDialog& dialog, const QString& text) {
    for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

// The details label is the only label carrying markup; the instruction line has none.
QString details_text(RestoreSnapshotDialog& dialog) {
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("<b>"))) {
            return label->text();
        }
    }
    return {};
}

}  // namespace

TEST_CASE("Restore dialog selects readable snapshots and disables Restore for unreadable ones",
          "[ui][restore_snapshot]") {
    [[maybe_unused]] auto& app = ensure_app();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString valid_snap = dir.filePath(QStringLiteral("ModsConfig_20260922_120000.xml"));
    write_snapshot(valid_snap, {QStringLiteral("mod.foo"), QStringLiteral("mod.bar")});
    const QString corrupt_snap = dir.filePath(QStringLiteral("ModsConfig_20260922_130000.xml"));
    write_text(corrupt_snap, "<<<INVALID XML>>>");

    RestoreSnapshotDialog dialog(QStringList{valid_snap, corrupt_snap});
    QListWidget* list = list_of(dialog);
    REQUIRE(list != nullptr);
    REQUIRE(list->count() == 2);

    // The first readable snapshot is selected initially.
    REQUIRE(dialog.selectedSnapshot() == valid_snap);
    QPushButton* restore_btn = button_labelled(dialog, QStringLiteral("Restore"));
    QPushButton* cancel_btn = button_labelled(dialog, QStringLiteral("Cancel"));
    REQUIRE(restore_btn != nullptr);
    REQUIRE(cancel_btn != nullptr);
    REQUIRE(restore_btn->isEnabled());
    REQUIRE(restore_btn->parentWidget() == &dialog);

    list->setCurrentRow(1);
    REQUIRE_FALSE(dialog.selectedSnapshot().has_value());
    REQUIRE_FALSE(restore_btn->isEnabled());

    list->setCurrentRow(0);
    REQUIRE(dialog.selectedSnapshot() == valid_snap);
    REQUIRE(restore_btn->isEnabled());

    cancel_btn->click();
    REQUIRE(dialog.result() == QDialog::Rejected);

    RestoreSnapshotDialog accept_dialog(QStringList{valid_snap});
    QListWidget* accept_list = list_of(accept_dialog);
    REQUIRE(accept_list != nullptr);
    emit accept_list->itemDoubleClicked(accept_list->item(0));
    REQUIRE(accept_dialog.result() == QDialog::Accepted);
    REQUIRE(accept_dialog.selectedSnapshot() == valid_snap);
}

TEST_CASE("Restore dialog Restore button accepts the selected snapshot", "[ui][restore_snapshot]") {
    [[maybe_unused]] auto& app = ensure_app();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString valid_snap = dir.filePath(QStringLiteral("ModsConfig_20260922_120000.xml"));
    write_snapshot(valid_snap, {QStringLiteral("mod.foo")});

    RestoreSnapshotDialog dialog(QStringList{valid_snap});
    QPushButton* restore_btn = button_labelled(dialog, QStringLiteral("Restore"));
    REQUIRE(restore_btn != nullptr);
    restore_btn->click();
    REQUIRE(dialog.result() == QDialog::Accepted);
    REQUIRE(dialog.selectedSnapshot() == valid_snap);
}

TEST_CASE("Restore dialog double-click on an unreadable snapshot does not accept", "[ui][restore_snapshot]") {
    [[maybe_unused]] auto& app = ensure_app();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString corrupt_snap = dir.filePath(QStringLiteral("ModsConfig_20260922_130000.xml"));
    write_text(corrupt_snap, "<<<INVALID XML>>>");

    RestoreSnapshotDialog dialog(QStringList{corrupt_snap});
    QListWidget* list = list_of(dialog);
    REQUIRE(list != nullptr);
    emit list->itemDoubleClicked(list->item(0));
    REQUIRE(dialog.result() == QDialog::Rejected);
    REQUIRE_FALSE(dialog.selectedSnapshot().has_value());
}

TEST_CASE("Restore dialog lists snapshot timestamps and mod counts in given order", "[ui][restore_snapshot]") {
    [[maybe_unused]] auto& app = ensure_app();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString two_mods = dir.filePath(QStringLiteral("ModsConfig_20260922_120000.xml"));
    write_snapshot(two_mods, {QStringLiteral("mod.foo"), QStringLiteral("mod.bar")});
    const QString one_mod = dir.filePath(QStringLiteral("ModsConfig_20260922_140000.xml"));
    write_snapshot(one_mod, {QStringLiteral("mod.foo")});
    const QString no_mods = dir.filePath(QStringLiteral("ModsConfig_20260922_150000_123456.xml"));
    write_snapshot(no_mods, {});
    const QString corrupt = dir.filePath(QStringLiteral("ModsConfig_20260922_130000.xml"));
    write_text(corrupt, "<<<INVALID XML>>>");

    RestoreSnapshotDialog dialog(QStringList{two_mods, one_mod, no_mods, corrupt});
    QListWidget* list = list_of(dialog);
    REQUIRE(list != nullptr);
    REQUIRE(list->count() == 4);

    const QString dash = QString::fromUtf8("\u2014");
    REQUIRE(list->item(0)->text() == QStringLiteral("2026-09-22 12:00:00 UTC ") + dash +
                                         QStringLiteral(" 2 active mods, RimWorld 1.5"));
    REQUIRE(list->item(1)->text() == QStringLiteral("2026-09-22 14:00:00 UTC ") + dash +
                                         QStringLiteral(" 1 active mod, RimWorld 1.5"));
    REQUIRE(list->item(2)->text() == QStringLiteral("2026-09-22 15:00:00 UTC ") + dash +
                                         QStringLiteral(" 0 active mods, RimWorld 1.5"));
    REQUIRE(list->item(3)->text() == QStringLiteral("2026-09-22 13:00:00 UTC ") + dash +
                                         QStringLiteral(" Cannot be read"));
    REQUIRE(list->item(0)->toolTip() == QStringLiteral("ModsConfig_20260922_120000.xml"));
}

TEST_CASE("Restore dialog falls back to file modification time for unnamed or invalid snapshot names",
          "[ui][restore_snapshot]") {
    [[maybe_unused]] auto& app = ensure_app();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString unnamed = dir.filePath(QStringLiteral("custom.xml"));
    write_snapshot(unnamed, {QStringLiteral("mod.foo")});
    const QString bad_date = dir.filePath(QStringLiteral("ModsConfig_20261322_120000.xml"));
    write_snapshot(bad_date, {QStringLiteral("mod.foo")});

    RestoreSnapshotDialog dialog(QStringList{unnamed, bad_date});
    QListWidget* list = list_of(dialog);
    REQUIRE(list != nullptr);
    REQUIRE(list->count() == 2);

    const QRegularExpression pattern(QStringLiteral(
        R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} UTC \x{2014} 1 active mod, RimWorld 1\.5$)"));
    REQUIRE(pattern.match(list->item(0)->text()).hasMatch());
    REQUIRE(pattern.match(list->item(1)->text()).hasMatch());
}

TEST_CASE("Restore dialog details describe the selected snapshot", "[ui][restore_snapshot]") {
    [[maybe_unused]] auto& app = ensure_app();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString valid_snap = dir.filePath(QStringLiteral("ModsConfig_20260922_120000.xml"));
    write_snapshot(valid_snap, {QStringLiteral("mod.foo"), QStringLiteral("mod.bar")});
    const QString corrupt_snap = dir.filePath(QStringLiteral("ModsConfig_20260922_130000.xml"));
    write_text(corrupt_snap, "<<<INVALID XML>>>");

    RestoreSnapshotDialog dialog(QStringList{valid_snap, corrupt_snap});
    QListWidget* list = list_of(dialog);
    REQUIRE(list != nullptr);

    REQUIRE(details_text(dialog) ==
            QStringLiteral("<b>File:</b> ModsConfig_20260922_120000.xml<br><b>Active mods:</b> 2 | "
                           "<b>Game version:</b> 1.5"));

    list->setCurrentRow(1);
    REQUIRE(details_text(dialog) ==
            QStringLiteral("<b>This saved mod list cannot be read.</b> ModsConfig_20260922_130000.xml "
                           "will not be restored."));

    list->setCurrentItem(nullptr);
    REQUIRE_FALSE(dialog.selectedSnapshot().has_value());
    REQUIRE(details_text(dialog).isEmpty());
}

TEST_CASE("Restore dialog with no snapshots has nothing to select", "[ui][restore_snapshot]") {
    [[maybe_unused]] auto& app = ensure_app();

    RestoreSnapshotDialog dialog(QStringList{});
    QListWidget* list = list_of(dialog);
    REQUIRE(list != nullptr);
    REQUIRE(list->count() == 0);
    REQUIRE_FALSE(dialog.selectedSnapshot().has_value());
    QPushButton* restore_btn = button_labelled(dialog, QStringLiteral("Restore"));
    REQUIRE(restore_btn != nullptr);
    REQUIRE_FALSE(restore_btn->isEnabled());
}

TEST_CASE("Confirm restore dialog names the snapshot and defaults to Cancel", "[ui][restore_snapshot]") {
    [[maybe_unused]] auto& app = ensure_app();

    ConfirmRestoreDialog confirm(QStringLiteral("ModsConfig_20260922_120000.xml"));
    REQUIRE(confirm.windowTitle() == QStringLiteral("Restore Mod List"));
    REQUIRE(confirm.text().contains(QStringLiteral("ModsConfig_20260922_120000.xml")));
    REQUIRE(confirm.informativeText().contains(QStringLiteral("backup")));
    REQUIRE(confirm.icon() == QMessageBox::Question);
    REQUIRE(confirm.defaultButton() == confirm.button(QMessageBox::Cancel));

    auto* restore_btn = qobject_cast<QPushButton*>(confirm.button(QMessageBox::Yes));
    REQUIRE(restore_btn != nullptr);
    REQUIRE(restore_btn->text() == QStringLiteral("Restore"));
    REQUIRE(restore_btn->objectName() == QStringLiteral("primaryAction"));
    auto* cancel_btn = qobject_cast<QPushButton*>(confirm.button(QMessageBox::Cancel));
    REQUIRE(cancel_btn != nullptr);
    REQUIRE(cancel_btn->text() == QStringLiteral("Cancel"));
}
