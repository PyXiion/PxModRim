#include "ui/window/actions.hpp"

#include <QAction>
#include <QKeySequence>
#include <QWidget>

namespace px::modrim {

QString ActionSpec::tooltip() const {
    return shortcut.isEmpty() ? label
        : QStringLiteral("%1 (%2)").arg(label, QKeySequence(shortcut).toString(QKeySequence::NativeText));
}

const std::array<ActionSpec, 18>& actionSpecs() {
    static const std::array<ActionSpec, 18> specs{{
        {ActionId::kSave, "save", "&Save Mod List", "Save mod list", "Ctrl+S"},
        {ActionId::kRestore, "restore", "&Restore Mod List…", "Restore mod list", {}},
        {ActionId::kSettings, "settings", "&Settings…", "Settings", "Ctrl+,"},
        {ActionId::kQuit, "quit", "&Quit", "Quit", "Ctrl+Q"},
        {ActionId::kRefresh, "refresh", "&Refresh Mods", "Refresh mods", "F5"},
        {ActionId::kFullRescan, "full_rescan", "&Full Mod Rescan", "Full mod rescan", "Ctrl+Shift+R"},
        {ActionId::kAutoSort, "auto_sort", "&Auto-Sort Mods", "Auto-sort mods", {}},
        {ActionId::kUpdateMods, "update_mods", "&Update Mods", "Update downloaded mods", {}},
        {ActionId::kFocusSearch, "focus_search", "Focus &Search", "Focus search", "Ctrl+F"},
        {ActionId::kNextView, "next_view", "&Next View", "Next view", "Ctrl+Tab"},
        {ActionId::kPrevView, "prev_view", "&Previous View", "Previous view", "Ctrl+Shift+Tab"},
        {ActionId::kFullscreen, "fullscreen", "Toggle &Fullscreen", "Toggle fullscreen", "F11"},
        {ActionId::kReportIssue, "report_issue", "&Report Issue", "Report issue", {}},
        {ActionId::kUploadLogs, "upload_logs", "Log && System &Info…", "Log & system info", {}},
        {ActionId::kOpenLogs, "open_logs", "Open &Logs Folder", "Open logs folder", {}},
        {ActionId::kShortcuts, "shortcuts", "&Keyboard Shortcuts", "Keyboard shortcuts", {}},
        {ActionId::kCheckUpdates, "check_updates", "Check for &Updates…", "Check for updates", {}},
        {ActionId::kAbout, "about", "&About PxModRim", "About PxModRim", "F1"},
    }};
    return specs;
}

const ActionSpec& actionSpec(ActionId id) {
    return actionSpecs()[static_cast<std::size_t>(id)];
}

ActionMap createActions(QWidget& window) {
    ActionMap actions;
    for (const auto& spec : actionSpecs()) {
        auto* action = new QAction(spec.menuText, &window);
        if (!spec.shortcut.isEmpty()) action->setShortcut(QKeySequence(spec.shortcut));
        window.addAction(action);
        actions.emplace(spec.id, action);
    }
    return actions;
}

QList<QPair<QString, QString>> shortcutRows() {
    QList<QPair<QString, QString>> rows;
    for (const auto& spec : actionSpecs()) {
        if (!spec.shortcut.isEmpty()) {
            rows.append({QKeySequence(spec.shortcut).toString(QKeySequence::NativeText), spec.label});
        }
    }
    rows.append({QStringLiteral("Ctrl+1–9"), QStringLiteral("Switch to view")});
    rows.append({QStringLiteral("Alt"), QStringLiteral("Show or hide menu bar")});
    rows.append({QStringLiteral("Esc"), QStringLiteral("Hide menu bar")});
    return rows;
}

}  // namespace px::modrim
