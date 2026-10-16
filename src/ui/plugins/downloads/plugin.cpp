#include "ui/plugins/downloads/plugin.h"

#include "ui/plugins/downloads/view.h"

namespace px::modrim {

DownloadsUiPlugin::DownloadsUiPlugin() : UiPlugin(QStringLiteral("downloads_ui"), {QStringLiteral("downloads")}) {}
void DownloadsUiPlugin::setup(AppContext& ctx) { ctx.addRailView<DownloadsViewPanel>(); }

}  // namespace px::modrim
