#pragma once

#include <QString>
#include <memory>
#include <optional>

#include <pugixml.hpp>

#include "core/error.hpp"
#include "core/models/metadata/structures.hpp"

namespace px::modrim {

// A missing or unreadable About.xml yields an invalid mod, not an error.
// Only a path that is not a directory is rejected.
Result<ListedModPtr> createListedModFromPath(
    const QString& path, const QString& targetVersion,
    bool preferVersioned = true,
    std::optional<QString> aboutXmlPath = std::nullopt);

// Parses metadata without assigning a directory to the resulting mod.
std::shared_ptr<AboutXmlMod> createAboutModFromElement(
    pugi::xml_node root, const QString& targetVersion,
    bool preferVersioned = true);

}  // namespace px::modrim
