#pragma once

#include "ui/context.h"

#ifdef PX_HAS_STEAM
#include "core/downloads/steam.h"
#include "ui/settings_section.h"

namespace px::ui {

class SteamSettingsSection final : public PluginConfigSection<core::SteamSettings> {
public:
    SteamSettingsSection(core::PluginConfig<core::SteamSettings>& settings, QObject* parent);
    QString title() const override;
    QUrl source() const override;
};

class SteamDownloaderUiPlugin final : public UiPlugin {
public:
    SteamDownloaderUiPlugin();
    void setup(AppContext& ctx) override;
};

}  // namespace px::ui
#endif
