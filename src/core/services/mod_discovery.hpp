#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/models/metadata/structures.hpp"

namespace px::modrim::services {

// Parses ModsConfig.xml inside `config_folder` and returns the ordered list of UUIDs matching
// active mods. If multiple discovered mods share a package id, the first match (in sorted UUID
// order) is chosen. The "_steam" suffix on package ids is ignored.
QStringList resolveActiveUuids(const QHash<QString, ListedModPtr>& allMods,
                                 const QString& configFolder);

}  // namespace px::modrim::services
