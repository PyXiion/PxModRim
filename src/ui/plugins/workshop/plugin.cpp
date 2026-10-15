#include "ui/plugins/workshop/plugin.h"

#include "ui/plugins/workshop/view.h"

namespace px::ui::workshop {

CatalogSettingsSection::CatalogSettingsSection(core::workshop::WorkshopCatalog& catalog, QObject* parent)
    : PluginConfigSectionBase({{QStringLiteral("url"), QString::fromStdString(catalog.settings().url)}}, parent), catalog_(catalog) {}
QString CatalogSettingsSection::title() const { return QStringLiteral("Workshop catalog"); }
QUrl CatalogSettingsSection::source() const { return QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/CatalogSettings.qml")); }
core::Result<void> CatalogSettingsSection::commit(const QJsonObject& merged) {
    catalog_.set_base_url(merged.value(QStringLiteral("url")).toString());
    return {};
}

WorkshopUiPlugin::WorkshopUiPlugin()
    : UiPlugin(QStringLiteral("workshop_ui"), {QStringLiteral("workshop_catalog"), QStringLiteral("downloads")}) {}
void WorkshopUiPlugin::setup(AppContext& ctx) {
    ctx.addRailView<WorkshopViewPanel>();
    auto* catalog = static_cast<core::workshop::WorkshopCatalog*>(ctx.coreContext().plugins().get(QStringLiteral("workshop_catalog")));
    ctx.addSettingsSection([catalog](QObject* parent) { return new CatalogSettingsSection(*catalog, parent); });
}

}  // namespace px::ui::workshop
