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

namespace px::modrim {

enum class ActionId {
    kSave, kRestore, kSettings, kQuit, kRefresh, kFullRescan, kAutoSort, kUpdateMods,
    kFocusSearch, kNextView, kPrevView, kFullscreen, kReportIssue, kUploadLogs,
    kOpenLogs, kShortcuts, kCheckUpdates, kAbout,
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

}  // namespace px::modrim
