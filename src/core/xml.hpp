#pragma once

#include <QString>

#include <glaze/json/json_t.hpp>
#include <pugixml.hpp>

#include "core/error.hpp"

namespace px::modrim {

Result<void> loadXmlFile(pugi::xml_document& doc, const QString& path);
QString rawText(pugi::xml_node node);
glz::json_t elementToJson(pugi::xml_node element);
Result<glz::json_t> xmlFileToJson(const QString& path);

}  // namespace px::modrim
