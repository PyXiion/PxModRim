#include "ui/plugins/steam_downloader/plugin.h"

#ifdef PX_HAS_STEAM
namespace px::ui {

SteamSettingsSection::SteamSettingsSection(core::PluginConfig<core::SteamSettings>& settings, QObject* parent)
    : PluginConfigSection(settings, parent) {}
QString SteamSettingsSection::title() const { return QStringLiteral("Steam Workshop"); }
QUrl SteamSettingsSection::source() const { return QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/SteamSettings.qml")); }

SteamDownloaderUiPlugin::SteamDownloaderUiPlugin()
    : UiPlugin(QStringLiteral("steam_downloader_ui"), {QStringLiteral("steam_downloader")}) {}

void SteamDownloaderUiPlugin::setup(AppContext& ctx) {
    auto& downloader = *static_cast<core::SteamDownloader*>(ctx.coreContext().plugins().get(QStringLiteral("steam_downloader")));
    ctx.addSettingsSection([&downloader](QObject* parent) { return new SteamSettingsSection(downloader.settings(), parent); });
}

}  // namespace px::ui
#endif
