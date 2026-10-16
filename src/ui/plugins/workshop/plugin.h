#pragma once

#include "core/workshop/catalog.h"
#include "ui/context.h"
#include "ui/settings_section.h"

namespace px::modrim::workshop {

class CatalogSettingsSection : public PluginConfigSectionBase {
public:
    CatalogSettingsSection(workshop::WorkshopCatalog& catalog, QObject* parent);
    QString title() const override;
    QUrl source() const override;

protected:
    Result<void> commit(const QJsonObject& merged) override;

private:
    workshop::WorkshopCatalog& catalog_;
};

class WorkshopUiPlugin : public UiPlugin {
public:
    WorkshopUiPlugin();
    void setup(AppContext& ctx) override;
};

}  // namespace px::modrim::workshop
