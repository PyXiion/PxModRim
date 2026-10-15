#pragma once

#include <array>
#include <map>
#include <optional>
#include <vector>

#include <QList>
#include <QString>
#include <QPair>

class QAction;
class QWidget;

namespace px::ui {

enum class ActionId {
    Save, Restore, Settings, Quit, Refresh, FullRescan, AutoSort, UpdateMods,
    FocusSearch, NextView, PrevView, Fullscreen, ReportIssue, UploadLogs,
    OpenLogs, Shortcuts, CheckUpdates, About,
};

struct ActionSpec {
    ActionId id;
    QString key;
    QString menuText;
    QString label;
    QString shortcut;
    QString tooltip() const;
};

using ActionMap = std::map<ActionId, QAction*>;
const std::array<ActionSpec, 18>& actionSpecs();
const ActionSpec& actionSpec(ActionId id);
ActionMap createActions(QWidget& window);
QList<QPair<QString, QString>> shortcutRows();

}  // namespace px::ui
