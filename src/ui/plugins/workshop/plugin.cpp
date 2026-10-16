#include "ui/plugins/workshop/plugin.hpp"

#include "ui/plugins/workshop/view.hpp"

namespace px::modrim::workshop {

CatalogSettingsSection::CatalogSettingsSection(workshop::WorkshopCatalog& catalog, QObject* parent)
    : PluginConfigSectionBase({{QStringLiteral("url"), QString::fromStdString(catalog.settings().url)}}, parent), m_catalog(catalog) {}
QString CatalogSettingsSection::title() const { return QStringLiteral("Workshop catalog"); }
QUrl CatalogSettingsSection::source() const { return QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/CatalogSettings.qml")); }
Result<void> CatalogSettingsSection::commit(const QJsonObject& merged) {
    m_catalog.setBaseUrl(merged.value(QStringLiteral("url")).toString());
    return {};
}

WorkshopUiPlugin::WorkshopUiPlugin()
    : UiPlugin(QStringLiteral("workshop_ui"), {QStringLiteral("workshop_catalog"), QStringLiteral("downloads")}) {}
void WorkshopUiPlugin::setup(AppContext& ctx) {
    ctx.addRailView<WorkshopViewPanel>();
    auto* catalog = static_cast<workshop::WorkshopCatalog*>(ctx.coreContext().plugins().get(QStringLiteral("workshop_catalog")));
    ctx.addSettingsSection([catalog](QObject* parent) { return new CatalogSettingsSection(*catalog, parent); });
}

}  // namespace px::modrim::workshop
