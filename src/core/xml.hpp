#pragma once

#include <QString>
#include <optional>

#include <glaze/json/json_t.hpp>
#include <pugixml.hpp>

#include "core/error.hpp"

namespace px::modrim {

Result<void> loadXmlFile(pugi::xml_document& doc, const QString& path);
QString rawText(pugi::xml_node node);
std::optional<QString> childText(pugi::xml_node parent, const char* tag);
glz::json_t elementToJson(pugi::xml_node element);
Result<glz::json_t> xmlFileToJson(const QString& path);
Result<void> appendJsonAsXml(pugi::xml_node parent, const glz::json_t& data);

}  // namespace px::modrim
