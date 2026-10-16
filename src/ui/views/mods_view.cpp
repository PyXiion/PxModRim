#include "ui/views/mods_view.hpp"
#include "ui/theme/constants.hpp"
#include <QHBoxLayout>
namespace px::modrim {
ModsViewPanel::ModsViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : BaseViewPanel(ctx,engine,parent,app) {
    auto* content = new QWidget(this); content->setObjectName(QStringLiteral("contentArea"));
    auto* layout = new QHBoxLayout(content); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    sidebar = new SidebarPanel(ctx,engine,content); sidebar->setObjectName(QStringLiteral("sidebarPanel")); sidebar->setFixedWidth(kSidebarWidth);
    connect(sidebar,&SidebarPanel::entrySelected,this,&ModsViewPanel::entrySelected); layout->addWidget(sidebar);
    modList = new ModListPanel(ctx,engine,content); modList->setObjectName(QStringLiteral("modListPanel"));
    connect(modList,&ModListPanel::modPicked,this,&ModsViewPanel::modSelected);
    connect(modList,&ModListPanel::activeModsChanged,this,&ModsViewPanel::activeModsChanged,Qt::QueuedConnection);
    connect(modList,&ModListPanel::orderChanged,this,&ModsViewPanel::orderChanged,Qt::QueuedConnection);
    layout->addWidget(modList,3);
    modInfo = new ModInfoPanel(ctx,engine,app.uiPrefs(),content); modInfo->setObjectName(QStringLiteral("modInfoPanel")); modInfo->setMinimumWidth(300);
    layout->addWidget(modInfo,2); m_root->addWidget(content,1);
}
void ModsViewPanel::applySidebarEntry(const view::SidebarEntry& entry) {
    modList->clearSelection();
    modList->setSidebarFilter(entry.visibleUuids);
}
void ModsViewPanel::applyCurrentSidebarFilter() {
    if (const auto entry = sidebar->currentEntry()) modList->setSidebarFilter(entry->visibleUuids);
}
}
