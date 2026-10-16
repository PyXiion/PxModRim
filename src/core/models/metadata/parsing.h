#pragma once

#include <memory>
#include <optional>

#include <QString>
#include <pugixml.hpp>

#include "core/error.h"
#include "core/models/metadata/structures.h"

namespace px::modrim {

// Builds a mod for the directory `path`, parsing its About.xml (found automatically, or the
// explicit `aboutXmlPath`). Returns Err only when `path` is not a directory. A mod whose metadata
// is unusable is still returned, with `valid == false`.
//
// `targetVersion` selects `*ByVersion` entries: "1.5" matches `<v1.5>`. When
// `preferVersioned` is false those entries are ignored.
Result<ListedModPtr> createListedModFromPath(const QString& path, const QString& targetVersion,
                                             bool preferVersioned = true,
                                             std::optional<QString> aboutXmlPath = std::nullopt);

// Builds an AboutXmlMod from the `<ModMetaData>` root element of an About.xml. The result's
// `valid` flag reports whether the metadata is usable; no mod path is set.
std::shared_ptr<AboutXmlMod> createAboutModFromElement(pugi::xml_node root,
                                                       const QString& targetVersion,
                                                       bool preferVersioned = true);

}  // namespace px::modrim
