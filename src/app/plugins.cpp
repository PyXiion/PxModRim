#include "app/plugins.h"

#include <memory>

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <spdlog/spdlog.h>

#include "core/config.h"
#include "core/context.h"
#include "core/organizer/db.h"
#include "core/organizer/service.h"
#include "core/workshop/catalog.h"
#include "ui/plugins/downloads/plugin.h"
#include "ui/plugins/steam_downloader/plugin.h"
#include "ui/plugins/steam_workshop/plugin.h"
#include "ui/plugins/workshop/plugin.h"
#include "ui/plugins/organizer/plugin.h"
#include "ui/views/mods_view.h"

#ifdef PX_HAS_STEAM
#include "core/downloads/steam.h"
#endif

namespace px::modrim {

std::expected<QSet<QString>, px::modrim::Error> parseDisabledPlugins(const QString& envValue,
                                                                  const QString& manifestPath) {
    QSet<QString> disabled;
    for (const QString& part : envValue.split(QLatin1Char(','))) {
        const QString name = part.trimmed();
        if (!name.isEmpty()) {
            disabled.insert(name);
        }
    }

    if (!QFileInfo(manifestPath).isFile()) {
        return disabled;
    }
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::unexpected(px::modrim::Error{QStringLiteral("Cannot read %1").arg(manifestPath)});
    }

    const QString variant = QString::fromUtf8(manifest.readAll()).trimmed();
    if (variant == QLatin1StringView("NoWorkshop")) {
        disabled.unite(QSet<QString>{QStringLiteral("steam_downloader"), QStringLiteral("steamworkshop"),
                                     QStringLiteral("downloads_ui"), QStringLiteral("workshop_catalog"),
                                     QStringLiteral("workshop_ui")});
    } else if (variant == QLatin1StringView("NativeWorkshop")) {
        disabled.insert(QStringLiteral("steamworkshop"));
    } else if (variant == QLatin1StringView("SteamWorkshop")) {
        disabled.unite(QSet<QString>{QStringLiteral("workshop_catalog"), QStringLiteral("workshop_ui")});
    } else {
        return std::unexpected(px::modrim::Error{QStringLiteral("Unknown workshop variant: %1").arg(variant)});
    }
    return disabled;
}

QStringList registerCorePlugins(px::modrim::CoreContext& ctx, px::modrim::ConfigService& config,
                                px::modrim::organizer::OrganizerDb& organizerDb, const QSet<QString>& disabled) {
    ctx.registerPlugin(std::make_unique<px::modrim::organizer::OrganizerService>(ctx, organizerDb));

    QStringList downloaders;
    if (!disabled.contains(QStringLiteral("steam_downloader"))) {
#ifdef PX_HAS_STEAM
        ctx.registerPlugin(std::make_unique<px::modrim::SteamDownloader>(px::modrim::SteamDownloaderDeps{
            ctx.downloads(),
            config,
            [&ctx] { return QString::fromStdString(ctx.config().paths.local); },
            [&ctx] { return ctx.allMods().values(); },
        }));
        downloaders.append(QStringLiteral("steam"));
#else
        spdlog::info("PxSteamDL is not built in; Steam downloads disabled");
#endif
    }

    // The catalog is only useful with a downloader to fetch items.
    if (!downloaders.isEmpty() && !disabled.contains(QStringLiteral("workshop_catalog"))) {
        ctx.registerPlugin(std::make_unique<px::modrim::workshop::WorkshopCatalog>());
    }
    return downloaders;
}

// How to add a UI plugin (mirrors the legacy _app.py `_setup`):
//   - Guard it exactly as _app.py did, e.g.
//       if (!selection.disabled.contains(QStringLiteral("workshop_ui")) && selection.downloaders.contains("steam"))
//           app.registerPlugin(std::make_unique<px::modrim::WorkshopUiPlugin>());
//   - Rail views go through app.addRailView<View>(), and must come before the plugins
//     that read the rail.
// Each port adds one line here; no other file changes.
void registerUiPlugins(px::modrim::AppContext& ctx, const PluginSelection& selection) {
    ctx.addRailView<px::modrim::ModsViewPanel>();
    ctx.registerPlugin(std::make_unique<px::modrim::OrganizerUiPlugin>());
#ifdef PX_HAS_STEAM
    if (selection.downloaders.contains(QStringLiteral("steam"))) ctx.registerPlugin(std::make_unique<px::modrim::SteamDownloaderUiPlugin>());
    if (selection.downloaders.contains(QStringLiteral("steam")) && !selection.disabled.contains(QStringLiteral("steamworkshop"))) ctx.registerPlugin(std::make_unique<px::modrim::SteamWorkshopUiPlugin>());
#endif
    if (!selection.downloaders.isEmpty() && !selection.disabled.contains(QStringLiteral("downloads_ui"))) ctx.registerPlugin(std::make_unique<px::modrim::DownloadsUiPlugin>());
    if (!selection.downloaders.isEmpty() && !selection.disabled.contains(QStringLiteral("workshop_catalog")) && !selection.disabled.contains(QStringLiteral("workshop_ui"))) ctx.registerPlugin(std::make_unique<px::modrim::workshop::WorkshopUiPlugin>());
}

}  // namespace px::modrim
