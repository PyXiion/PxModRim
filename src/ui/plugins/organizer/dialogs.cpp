#include "ui/plugins/organizer/dialogs.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "core/organizer/models.hpp"
#include "core/organizer/resolve.hpp"

namespace px::modrim {

namespace {

using px::modrim::organizer::FolderNode;
using px::modrim::organizer::kRootId;

QPushButton* makeButton(const QString& text, QWidget* parent,
                        const QString& objectName) {
  auto* button = new QPushButton(text, parent);
  button->setObjectName(objectName);
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

void addFolder(QTreeWidgetItem* parent, const FolderNode& node,
               const QSet<qint64>& allowed) {
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

FolderNameDialog::FolderNameDialog(const QString& title, const QString& initial,
                                   QWidget* parent, const QString& verb)
    : QDialog(parent) {
  setWindowTitle(title);
  setMinimumWidth(360);
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("Folder name"), this));
  m_edit = new QLineEdit(initial, this);
  layout->addWidget(m_edit);

  const QString primaryText =
      verb.isEmpty() ? title.section(u' ', 0, 0, QString::SectionSkipEmpty)
                     : verb;
  auto* ok = makeButton(primaryText, this, QStringLiteral("primaryAction"));
  ok->setDefault(true);
  auto* cancel =
      makeButton(QStringLiteral("Cancel"), this, QStringLiteral("appBtn"));
  connect(ok, &QPushButton::clicked, this, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  addButtonRow(layout, cancel, ok);

  auto validate = [ok](const QString& text) {
    ok->setEnabled(!text.trimmed().isEmpty());
  };
  connect(m_edit, &QLineEdit::textChanged, this, validate);
  connect(m_edit, &QLineEdit::returnPressed, ok, &QPushButton::click);
  validate(initial);
  m_edit->selectAll();
}

QString FolderNameDialog::textValue() const { return m_edit->text().trimmed(); }

DeleteFolderDialog::DeleteFolderDialog(const QString& name, QWidget* parent)
    : QMessageBox(parent) {
  setIcon(QMessageBox::Warning);
  setWindowTitle(QStringLiteral("Delete Folder"));
  setText(QStringLiteral("Delete ‘%1’ and its subfolders?").arg(name));
  setInformativeText(QStringLiteral(
      "Their mods become ungrouped. Load order is not affected."));
  setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
  setDefaultButton(QMessageBox::Cancel);
  setEscapeButton(QMessageBox::Cancel);
  if (auto* yes = qobject_cast<QPushButton*>(button(QMessageBox::Yes))) {
    yes->setText(QStringLiteral("Delete"));
    yes->setObjectName(QStringLiteral("primaryAction"));
  }
}

FolderPickerDialog::FolderPickerDialog(const FolderNode& tree,
                                       const QString& title,
                                       const QSet<qint64>& allowed,
                                       QWidget* parent, const QString& verb)
    : QDialog(parent) {
  setWindowTitle(title);
  resize(380, 450);
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("Destination folder"), this));
  m_tree = new QTreeWidget(this);
  m_tree->setHeaderHidden(true);
  m_tree->setRootIsDecorated(true);
  layout->addWidget(m_tree);

  auto* top = new QTreeWidgetItem(
      m_tree, QStringList{QStringLiteral("Ungrouped (top level)")});
  top->setData(0, Qt::UserRole, kRootId);
  if (!allowed.contains(kRootId)) {
    top->setDisabled(true);
  }
  for (const FolderNode& folder : tree.children) {
    addFolder(top, folder, allowed);
  }
  m_tree->expandAll();

  auto* ok = makeButton(verb, this, QStringLiteral("primaryAction"));
  ok->setDefault(true);
  ok->setEnabled(false);
  auto* cancel =
      makeButton(QStringLiteral("Cancel"), this, QStringLiteral("appBtn"));

  auto acceptValid = [this, ok] {
    if (ok->isEnabled()) {
      accept();
    }
  };
  connect(m_tree, &QTreeWidget::currentItemChanged, this,
          [ok](QTreeWidgetItem* item) {
            ok->setEnabled(item != nullptr && !item->isDisabled());
          });
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this, acceptValid);
  connect(ok, &QPushButton::clicked, this, acceptValid);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  addButtonRow(layout, cancel, ok);
}

qint64 FolderPickerDialog::folderId() const {
  QTreeWidgetItem* item = m_tree->currentItem();
  Q_ASSERT(item != nullptr);
  return item->data(0, Qt::UserRole).toLongLong();
}

}  // namespace px::modrim
