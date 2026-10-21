#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

#include "core/error.hpp"

namespace px::modrim {

class AppContext;
class ConfigService;
class CoreContext;

namespace organizer {
class OrganizerDb;
}

// Which plugins this run keeps, and which downloaders came up. Core
// registration produces `downloaders`; UI registration reads both.
struct PluginSelection {
  QSet<QString> disabled;
  // Downloader names, e.g. "steam". Empty when no downloader is available.
  QStringList downloaders;
};

// Names from PX_DISABLED_PLUGINS (comma-separated) plus the plugins the baked
// workshop variant omits. `manifestPath` is the workshop-variant.txt file; a
// missing file bakes nothing. Fails on an unknown variant name.
Result<QSet<QString>> parseDisabledPlugins(const QString& envValue,
                                           const QString& manifestPath);

// Registers core plugins in legacy _app.py order and returns the downloaders
// that came up. `organizerDb` is not owned and must outlive `ctx`.
QStringList registerCorePlugins(px::modrim::CoreContext& ctx,
                                px::modrim::ConfigService& config,
                                px::modrim::organizer::OrganizerDb& organizerDb,
                                const QSet<QString>& disabled);

// Registers the UI plugins and rail views. Add one line per plugin here,
// guarded by the plugin's name in `selection.disabled` and the downloaders it
// needs.
void registerUiPlugins(AppContext& app, const PluginSelection& selection);

}  // namespace px::modrim
