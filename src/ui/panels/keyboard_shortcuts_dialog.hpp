#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

namespace px::modrim {
class KeyboardShortcutsDialog : public QDialog {
 public:
  explicit KeyboardShortcutsDialog(const QList<QPair<QString, QString>>& rows,
                                   QWidget* parent = nullptr);
};
}  // namespace px::modrim
