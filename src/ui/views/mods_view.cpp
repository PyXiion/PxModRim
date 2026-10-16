#include "ui/views/mods_view.h"
#include "ui/theme/constants.h"
#include <QHBoxLayout>
namespace px::modrim {
ModsViewPanel::ModsViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : BaseViewPanel(ctx,engine,parent,app) {
    auto* content = new QWidget(this); content->setObjectName(QStringLiteral("contentArea"));
    auto* layout = new QHBoxLayout(content); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    sidebar = new SidebarPanel(ctx,engine,content); sidebar->setObjectName(QStringLiteral("sidebarPanel")); sidebar->setFixedWidth(SIDEBAR_WIDTH);
    connect(sidebar,&SidebarPanel::entry_selected,this,&ModsViewPanel::entry_selected); layout->addWidget(sidebar);
    mod_list = new ModListPanel(ctx,engine,content); mod_list->setObjectName(QStringLiteral("modListPanel"));
    connect(mod_list,&ModListPanel::mod_selected,this,&ModsViewPanel::mod_selected);
    connect(mod_list,&ModListPanel::active_mods_changed,this,&ModsViewPanel::active_mods_changed,Qt::QueuedConnection);
    connect(mod_list,&ModListPanel::order_changed,this,&ModsViewPanel::order_changed,Qt::QueuedConnection);
    layout->addWidget(mod_list,3);
    mod_info = new ModInfoPanel(ctx,engine,app.uiPrefs(),content); mod_info->setObjectName(QStringLiteral("modInfoPanel")); mod_info->setMinimumWidth(300);
    layout->addWidget(mod_info,2); root_->addWidget(content,1);
}
void ModsViewPanel::apply_sidebar_entry(const view::SidebarEntry& entry) {
    mod_list->clearSelection();
    mod_list->set_sidebar_filter(entry.visible_uuids);
}
void ModsViewPanel::apply_current_sidebar_filter() {
    if (const auto entry = sidebar->current_entry()) mod_list->set_sidebar_filter(entry->visible_uuids);
}
}
