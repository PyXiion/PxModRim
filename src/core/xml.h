#pragma once

#include <optional>

#include <QString>
#include <glaze/json/json_t.hpp>
#include <pugixml.hpp>

#include "core/error.h"

namespace px::core {

// Loads and parses an XML file into `doc`. Fails if the file is missing, unreadable, malformed,
// or has no root element.
Result<void> loadXmlFile(pugi::xml_document& doc, const QString& path);

// Text before the first child node of `node` (lxml's `.text`), unstripped; empty when absent.
QString rawText(pugi::xml_node node);

// Stripped text of the first child element named `tag`, or nullopt when missing or blank.
std::optional<QString> childText(pugi::xml_node parent, const char* tag);

// Converts an element into the legacy dict shape: `{tag: value}`. Repeated child tags become
// arrays, attributes become "@name" keys, and mixed text becomes "#text".
glz::json_t elementToJson(pugi::xml_node element);

// Parses an XML file and returns `{rootTag: value}` as produced by elementToJson.
Result<glz::json_t> xmlFileToJson(const QString& path);

// Appends the one-key dict `{tag: value}` as an element under `parent` (the inverse of
// elementToJson). Fails unless `data` is an object with exactly one key.
Result<void> appendJsonAsXml(pugi::xml_node parent, const glz::json_t& data);

}  // namespace px::core
