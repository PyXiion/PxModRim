#include "ui/plugins/organizer/dialogs.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "core/organizer/models.h"
#include "core/organizer/resolve.h"

namespace px::ui {

namespace {

using px::core::organizer::FolderNode;
using px::core::organizer::ROOT_ID;

QPushButton* makeButton(const QString& text, QWidget* parent, const QString& object_name) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(object_name);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

void addButtonRow(QVBoxLayout* layout, QWidget* cancel, QWidget* confirm) {
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(cancel);
    row->addWidget(confirm);
    layout->addLayout(row);
}

void addFolder(QTreeWidgetItem* parent, const FolderNode& node, const QSet<qint64>& allowed) {
    auto* item = new QTreeWidgetItem(parent, QStringList{node.folder.name});
    item->setData(0, Qt::UserRole, node.folder.id);
    if (!allowed.contains(node.folder.id)) {
        item->setDisabled(true);
    }
    for (const FolderNode& child : node.children) {
        addFolder(item, child, allowed);
    }
}

}  // namespace

FolderNameDialog::FolderNameDialog(const QString& title, const QString& initial, QWidget* parent,
                                   const QString& verb)
    : QDialog(parent) {
    setWindowTitle(title);
    setMinimumWidth(360);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Folder name"), this));
    edit_ = new QLineEdit(initial, this);
    layout->addWidget(edit_);

    const QString primary_text =
        verb.isEmpty() ? title.section(u' ', 0, 0, QString::SectionSkipEmpty) : verb;
    auto* ok = makeButton(primary_text, this, QStringLiteral("primaryAction"));
    ok->setDefault(true);
    auto* cancel = makeButton(QStringLiteral("Cancel"), this, QStringLiteral("appBtn"));
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    addButtonRow(layout, cancel, ok);

    auto validate = [ok](const QString& text) { ok->setEnabled(!text.trimmed().isEmpty()); };
    connect(edit_, &QLineEdit::textChanged, this, validate);
    connect(edit_, &QLineEdit::returnPressed, ok, &QPushButton::click);
    validate(initial);
    edit_->selectAll();
}

QString FolderNameDialog::textValue() const {
    return edit_->text().trimmed();
}

DeleteFolderDialog::DeleteFolderDialog(const QString& name, QWidget* parent)
    : QMessageBox(parent) {
    setIcon(QMessageBox::Warning);
    setWindowTitle(QStringLiteral("Delete Folder"));
    setText(QStringLiteral("Delete ‘%1’ and its subfolders?").arg(name));
    setInformativeText(
        QStringLiteral("Their mods become ungrouped. Load order is not affected."));
    setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    setDefaultButton(QMessageBox::Cancel);
    setEscapeButton(QMessageBox::Cancel);
    if (auto* yes = qobject_cast<QPushButton*>(button(QMessageBox::Yes))) {
        yes->setText(QStringLiteral("Delete"));
        yes->setObjectName(QStringLiteral("primaryAction"));
    }
}

FolderPickerDialog::FolderPickerDialog(const FolderNode& tree, const QString& title,
                                       const QSet<qint64>& allowed, QWidget* parent,
                                       const QString& verb)
    : QDialog(parent) {
    setWindowTitle(title);
    resize(380, 450);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Destination folder"), this));
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(true);
    layout->addWidget(tree_);

    auto* top = new QTreeWidgetItem(tree_, QStringList{QStringLiteral("Ungrouped (top level)")});
    top->setData(0, Qt::UserRole, ROOT_ID);
    if (!allowed.contains(ROOT_ID)) {
        top->setDisabled(true);
    }
    for (const FolderNode& folder : tree.children) {
        addFolder(top, folder, allowed);
    }
    tree_->expandAll();

    auto* ok = makeButton(verb, this, QStringLiteral("primaryAction"));
    ok->setDefault(true);
    ok->setEnabled(false);
    auto* cancel = makeButton(QStringLiteral("Cancel"), this, QStringLiteral("appBtn"));

    auto accept_valid = [this, ok] {
        if (ok->isEnabled()) {
            accept();
        }
    };
    connect(tree_, &QTreeWidget::currentItemChanged, this, [ok](QTreeWidgetItem* item) {
        ok->setEnabled(item != nullptr && !item->isDisabled());
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, accept_valid);
    connect(ok, &QPushButton::clicked, this, accept_valid);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    addButtonRow(layout, cancel, ok);
}

qint64 FolderPickerDialog::folder_id() const {
    QTreeWidgetItem* item = tree_->currentItem();
    Q_ASSERT(item != nullptr);
    return item->data(0, Qt::UserRole).toLongLong();
}

}  // namespace px::ui
