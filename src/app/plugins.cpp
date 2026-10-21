#include "app/plugins.hpp"

#include <QFileInfo>
#include <QStringList>
#include <memory>

#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/context.hpp"
#include "core/file_io.hpp"
#include "core/organizer/db.hpp"
#include "core/organizer/service.hpp"
#include "core/workshop/catalog.hpp"
#include "ui/plugins/downloads/plugin.hpp"
#include "ui/plugins/organizer/plugin.hpp"
#include "ui/plugins/steam_downloader/plugin.hpp"
#include "ui/plugins/steam_workshop/plugin.hpp"
#include "ui/plugins/workshop/plugin.hpp"
#include "ui/views/mods_view.hpp"

#ifdef PX_HAS_STEAM
#include "core/downloads/steam.hpp"
#endif

namespace px::modrim {

Result<QSet<QString>> parseDisabledPlugins(const QString& envValue,
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
  const auto bytes = readFileBytes(manifestPath);
  if (!bytes) {
    return std::unexpected(
        Error{QStringLiteral("Cannot read %1").arg(manifestPath)});
  }

  const QString variant =
      QString::fromUtf8(bytes->data(), static_cast<qsizetype>(bytes->size()))
          .trimmed();
  if (variant == QLatin1StringView("NoWorkshop")) {
    disabled.unite(QSet<QString>{
        QStringLiteral("steam_downloader"), QStringLiteral("steamworkshop"),
        QStringLiteral("downloads_ui"), QStringLiteral("workshop_catalog"),
        QStringLiteral("workshop_ui")});
  } else if (variant == QLatin1StringView("NativeWorkshop")) {
    disabled.insert(QStringLiteral("steamworkshop"));
  } else if (variant == QLatin1StringView("SteamWorkshop")) {
    disabled.unite(QSet<QString>{QStringLiteral("workshop_catalog"),
                                 QStringLiteral("workshop_ui")});
  } else {
    return std::unexpected(
        Error{QStringLiteral("Unknown workshop variant: %1").arg(variant)});
  }
  return disabled;
}

QStringList registerCorePlugins(CoreContext& ctx, ConfigService& config,
                                organizer::OrganizerDb& organizerDb,
                                const QSet<QString>& disabled) {
  ctx.registerPlugin(
      std::make_unique<organizer::OrganizerService>(ctx, organizerDb));

  QStringList downloaders;
  if (!disabled.contains(QStringLiteral("steam_downloader"))) {
#ifdef PX_HAS_STEAM
    ctx.registerPlugin(std::make_unique<SteamDownloader>(SteamDownloaderDeps{
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
  if (!downloaders.isEmpty() &&
      !disabled.contains(QStringLiteral("workshop_catalog"))) {
    ctx.registerPlugin(std::make_unique<px::modrim::workshop::WorkshopCatalog>(
        &config, ctx.downloads(), [&ctx] { return ctx.allMods().values(); }));
  }
  return downloaders;
}

void registerUiPlugins(AppContext& ctx, const PluginSelection& selection) {
  ctx.addRailView<ModsViewPanel>();
  ctx.registerPlugin(std::make_unique<OrganizerUiPlugin>());
#ifdef PX_HAS_STEAM
  if (selection.downloaders.contains(QStringLiteral("steam")))
    ctx.registerPlugin(std::make_unique<SteamDownloaderUiPlugin>());
  if (selection.downloaders.contains(QStringLiteral("steam")) &&
      !selection.disabled.contains(QStringLiteral("steamworkshop")))
    ctx.registerPlugin(std::make_unique<SteamWorkshopUiPlugin>());
#endif
  if (!selection.downloaders.isEmpty() &&
      !selection.disabled.contains(QStringLiteral("downloads_ui")))
    ctx.registerPlugin(std::make_unique<DownloadsUiPlugin>());
  if (!selection.downloaders.isEmpty() &&
      !selection.disabled.contains(QStringLiteral("workshop_catalog")) &&
      !selection.disabled.contains(QStringLiteral("workshop_ui")))
    ctx.registerPlugin(std::make_unique<workshop::WorkshopUiPlugin>());
}

}  // namespace px::modrim
