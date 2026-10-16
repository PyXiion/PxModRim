#pragma once

#include "ui/context.hpp"

#ifdef PX_HAS_STEAM
#include "core/downloads/steam.hpp"
#include "ui/settings_section.hpp"

namespace px::modrim {

class SteamSettingsSection final : public PluginConfigSection<SteamSettings> {
public:
    SteamSettingsSection(PluginConfig<SteamSettings>& settings, QObject* parent);
    QString title() const override;
    QUrl source() const override;
};

class SteamDownloaderUiPlugin final : public UiPlugin {
public:
    SteamDownloaderUiPlugin();
    void setup(AppContext& ctx) override;
};

}  // namespace px::modrim
#endif
