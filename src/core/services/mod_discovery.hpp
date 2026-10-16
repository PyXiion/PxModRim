#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/models/metadata/structures.hpp"

namespace px::modrim::services {

// Parses ModsConfig.xml inside `config_folder` and returns the ordered list of UUIDs matching
// active mods. If multiple discovered mods share a package id, the first match (in sorted UUID
// order) is chosen. The "_steam" suffix on package ids is ignored.
QStringList resolve_active_uuids(const QHash<QString, ListedModPtr>& all_mods,
                                 const QString& config_folder);

}  // namespace px::modrim::services
