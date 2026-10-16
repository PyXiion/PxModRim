#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

namespace px::modrim {
inline const QList<QPair<QString, QString>> kQmlShortcuts{
    {QStringLiteral("Up / Down"), QStringLiteral("Navigate mod list")},
    {QStringLiteral("Shift+Up / Shift+Down"),
     QStringLiteral("Extend mod selection")},
    {QStringLiteral("Return / Space"), QStringLiteral("Toggle selected mods")},
    {QStringLiteral("Ctrl+A"), QStringLiteral("Select all mods")},
};

class KeyboardShortcutsDialog : public QDialog {
 public:
  explicit KeyboardShortcutsDialog(const QList<QPair<QString, QString>>& rows,
                                   QWidget* parent = nullptr);
};
}  // namespace px::modrim
