#include "ui/panels/keyboard_shortcuts_dialog.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace px::modrim {
KeyboardShortcutsDialog::KeyboardShortcutsDialog(const QList<QPair<QString, QString>>& rows,
                                                 QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(520, 600);
    auto* layout = new QVBoxLayout(this);
    auto* table = new QTableWidget(static_cast<int>(rows.size()), 2, this);
    table->setHorizontalHeaderLabels({tr("Shortcut"), tr("Action")});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (qsizetype row = 0; row < rows.size(); ++row) {
        table->setItem(static_cast<int>(row), 0, new QTableWidgetItem(rows[row].first));
        table->setItem(static_cast<int>(row), 1, new QTableWidgetItem(rows[row].second));
    }
    layout->addWidget(table);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName(QStringLiteral("primaryAction"));
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    layout->addLayout(buttons);
}
}  // namespace px::modrim
