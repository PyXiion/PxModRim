#pragma once

#include "core/workshop/catalog.h"
#include "ui/context.h"
#include "ui/settings_section.h"

namespace px::ui::workshop {

class CatalogSettingsSection : public PluginConfigSectionBase {
public:
    CatalogSettingsSection(core::workshop::WorkshopCatalog& catalog, QObject* parent);
    QString title() const override;
    QUrl source() const override;

protected:
    core::Result<void> commit(const QJsonObject& merged) override;

private:
    core::workshop::WorkshopCatalog& catalog_;
};

class WorkshopUiPlugin : public UiPlugin {
public:
    WorkshopUiPlugin();
    void setup(AppContext& ctx) override;
};

}  // namespace px::ui::workshop
