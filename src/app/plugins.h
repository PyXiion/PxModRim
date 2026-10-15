#pragma once

#include <expected>

#include <QSet>
#include <QString>
#include <QStringList>

#include "core/error.h"

namespace px::core {
class ConfigService;
class CoreContext;
namespace organizer {
class OrganizerDb;
}
}  // namespace px::core

namespace px::ui {
class AppContext;
}

namespace px::app {

// Which plugins this run keeps, and which downloaders came up. Core registration
// produces `downloaders`; UI registration reads both.
struct PluginSelection {
    QSet<QString> disabled;
    // Downloader names, e.g. "steam". Empty when no downloader is available.
    QStringList downloaders;
};

// Names from PX_DISABLED_PLUGINS (comma-separated) plus the plugins the baked
// workshop variant omits. `manifestPath` is the workshop-variant.txt file; a missing
// file bakes nothing. Fails on an unknown variant name.
std::expected<QSet<QString>, px::core::Error> parseDisabledPlugins(const QString& envValue,
                                                                  const QString& manifestPath);

// Registers core plugins in legacy _app.py order and returns the downloaders that came
// up. `organizerDb` is not owned and must outlive `ctx`.
QStringList registerCorePlugins(px::core::CoreContext& ctx, px::core::ConfigService& config,
                                px::core::organizer::OrganizerDb& organizerDb, const QSet<QString>& disabled);

// Registers the UI plugins and rail views. Add one line per plugin here, guarded the
// same way the legacy _app.py guarded it; see plugins.cpp for the pattern.
void registerUiPlugins(px::ui::AppContext& app, const PluginSelection& selection);

}  // namespace px::app
