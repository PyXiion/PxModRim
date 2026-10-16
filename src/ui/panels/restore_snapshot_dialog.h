#pragma once

#include <optional>
#include <vector>

#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include "core/models/metadata/structures.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace px::modrim {

// Lists saved mod lists (ModsConfig snapshots, in the order given, newest first) and
// lets the user pick one to restore. Snapshots that cannot be parsed are listed but
// cannot be selected.
class RestoreSnapshotDialog : public QDialog {
    Q_OBJECT

public:
    explicit RestoreSnapshotDialog(const QStringList& snapshots, QWidget* parent = nullptr);

    // The selected snapshot path, or nullopt while the selection is unreadable or absent.
    std::optional<QString> selectedSnapshot() const;

private slots:
    void onCurrentItemChanged(QListWidgetItem* current);
    void onRestoreClicked();

private:
    struct Snapshot {
        QString path;
        std::optional<ModsConfig> parsed;
    };

    void populateList();

    std::vector<Snapshot> snapshots_;
    std::optional<QString> selected_;
    QListWidget* list_ = nullptr;
    QLabel* details_ = nullptr;
    QPushButton* restore_ = nullptr;
};

// Asks before the current mod list is replaced by a snapshot.
class ConfirmRestoreDialog : public QMessageBox {
public:
    explicit ConfirmRestoreDialog(const QString& snapshotName, QWidget* parent = nullptr);
};

}  // namespace px::modrim
