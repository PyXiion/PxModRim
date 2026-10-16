#pragma once

#include <QDialog>
#include <QMessageBox>
#include <QSet>
#include <QString>
#include <QtGlobal>

class QLineEdit;
class QTreeWidget;

namespace px::modrim::organizer {
struct FolderNode;
}

namespace px::modrim {

// Asks for a folder name. Enter confirms; the confirm button is disabled while the
// name is blank. Without `verb`, the first word of `title` names the confirm button.
class FolderNameDialog : public QDialog {
public:
    FolderNameDialog(const QString& title, const QString& initial, QWidget* parent,
                     const QString& verb = {});

    // The entered name with surrounding whitespace removed.
    QString textValue() const;

private:
    QLineEdit* edit_ = nullptr;
};

// Confirms deleting a folder. Its mods become ungrouped; load order is unchanged.
// Yes deletes; Cancel and Escape keep the folder.
class DeleteFolderDialog : public QMessageBox {
public:
    DeleteFolderDialog(const QString& name, QWidget* parent);
};

// Picks a destination folder from `tree`'s children, with a top-level "Ungrouped"
// entry for ROOT_ID. Folders not in `allowed` are shown but cannot be chosen.
class FolderPickerDialog : public QDialog {
public:
    FolderPickerDialog(const px::modrim::organizer::FolderNode& tree, const QString& title,
                       const QSet<qint64>& allowed, QWidget* parent,
                       const QString& verb = QStringLiteral("Move"));

    // The id of the highlighted folder. Only meaningful once the confirm button is enabled.
    qint64 folder_id() const;

private:
    QTreeWidget* tree_ = nullptr;
};

}  // namespace px::modrim
