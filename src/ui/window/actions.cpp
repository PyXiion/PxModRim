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
        {ActionId::Save, "save", "&Save Mod List", "Save mod list", "Ctrl+S"},
        {ActionId::Restore, "restore", "&Restore Mod List…", "Restore mod list", {}},
        {ActionId::Settings, "settings", "&Settings…", "Settings", "Ctrl+,"},
        {ActionId::Quit, "quit", "&Quit", "Quit", "Ctrl+Q"},
        {ActionId::Refresh, "refresh", "&Refresh Mods", "Refresh mods", "F5"},
        {ActionId::FullRescan, "full_rescan", "&Full Mod Rescan", "Full mod rescan", "Ctrl+Shift+R"},
        {ActionId::AutoSort, "auto_sort", "&Auto-Sort Mods", "Auto-sort mods", {}},
        {ActionId::UpdateMods, "update_mods", "&Update Mods", "Update downloaded mods", {}},
        {ActionId::FocusSearch, "focus_search", "Focus &Search", "Focus search", "Ctrl+F"},
        {ActionId::NextView, "next_view", "&Next View", "Next view", "Ctrl+Tab"},
        {ActionId::PrevView, "prev_view", "&Previous View", "Previous view", "Ctrl+Shift+Tab"},
        {ActionId::Fullscreen, "fullscreen", "Toggle &Fullscreen", "Toggle fullscreen", "F11"},
        {ActionId::ReportIssue, "report_issue", "&Report Issue", "Report issue", {}},
        {ActionId::UploadLogs, "upload_logs", "Log && System &Info…", "Log & system info", {}},
        {ActionId::OpenLogs, "open_logs", "Open &Logs Folder", "Open logs folder", {}},
        {ActionId::Shortcuts, "shortcuts", "&Keyboard Shortcuts", "Keyboard shortcuts", {}},
        {ActionId::CheckUpdates, "check_updates", "Check for &Updates…", "Check for updates", {}},
        {ActionId::About, "about", "&About PxModRim", "About PxModRim", "F1"},
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
