#include "ui/plugins/downloads/plugin.hpp"

#include "ui/plugins/downloads/view.hpp"

namespace px::modrim {

DownloadsUiPlugin::DownloadsUiPlugin() : UiPlugin(QStringLiteral("downloads_ui"), {QStringLiteral("downloads")}) {}
void DownloadsUiPlugin::setup(AppContext& ctx) { ctx.addRailView<DownloadsViewPanel>(); }

}  // namespace px::modrim
