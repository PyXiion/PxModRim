#include "ui/panels/sidebar_panel.hpp"
#include "core/context.hpp"
#include "core/services/diagnostics_service.hpp"
#include <QQuickItem>
namespace px::modrim {
SidebarPanel::SidebarPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent)
    : FilterSidebar(engine, nullptr, parent), ctx_(ctx), model_(new SidebarModel(this)) {
    if (rootObject()) rootObject()->setProperty("model", QVariant::fromValue(model_));
    connect(this, &FilterSidebar::activated, this, &SidebarPanel::on_activated);
    sidebar_connection_ = ctx_.diagnosticsService().sidebar_entries_changed.connect([this](const auto& entries) { set_entries(entries); });
}
SidebarPanel::~SidebarPanel() { ctx_.diagnosticsService().sidebar_entries_changed.disconnect(sidebar_connection_); }
void SidebarPanel::set_entries(const QList<view::SidebarEntry>& entries) {
    const bool reset = entries.size() != entries_.size();
    entries_ = entries;
    model_->update_entries(entries);
    if (reset) if (const auto current = current_entry()) emit entry_selected(*current);
}
std::optional<view::SidebarEntry> SidebarPanel::current_entry() const {
    const int selected = current_index();
    if (selected >= 0 && selected < entries_.size()) return entries_[selected];
    return std::nullopt;
}
void SidebarPanel::select_all_entry() {
    auto* root = rootObject();
    auto* view = root ? root->findChild<QObject*>(QStringLiteral("listView")) : nullptr;
    if (!view) return;
    for (int row = 0; row < entries_.size(); ++row) if (entries_[row].kind == view::SidebarEntry::Kind::All) {
        view->setProperty("currentIndex", row); on_activated(row); return;
    }
}
void SidebarPanel::on_activated(int row) { if (row >= 0 && row < entries_.size()) emit entry_selected(entries_[row]); }
}
