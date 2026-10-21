#include "core/xml.hpp"

#include <QFileInfo>
#include <string>
#include <utility>
#include <variant>

#include "core/file_io.hpp"

namespace px::modrim {
namespace {

glz::json_t nodeValue(pugi::xml_node node) {
  glz::json_t::object_t fields;
  bool hasElements = false;
  for (const auto child : node.children()) {
    if (child.type() != pugi::node_element) continue;
    hasElements = true;
    auto value = nodeValue(child);
    auto [position, inserted] =
        fields.try_emplace(child.name(), std::move(value));
    if (inserted) continue;
    if (auto* repeated =
            std::get_if<glz::json_t::array_t>(&position->second.data)) {
      repeated->push_back(std::move(value));
    } else {
      glz::json_t::array_t entries;
      entries.push_back(std::move(position->second));
      entries.push_back(std::move(value));
      position->second.data = std::move(entries);
    }
  }
  const QString text = rawText(node).trimmed();
  glz::json_t result;
  if (!hasElements && !node.first_attribute()) {
    const auto first = node.first_child();
    if (first.type() == pugi::node_pcdata || first.type() == pugi::node_cdata) {
      result.data = text.toStdString();
    } else {
      result.data = glz::json_t::object_t{};
    }
    return result;
  }
  for (const auto attribute : node.attributes()) {
    fields[std::string("@") + attribute.name()].data =
        std::string(attribute.value());
  }
  if (!text.isEmpty()) fields["#text"].data = text.toStdString();
  result.data = std::move(fields);
  return result;
}

}  // namespace

Result<void> loadXmlFile(pugi::xml_document& doc, const QString& path) {
  if (!QFileInfo::exists(path)) {
    return std::unexpected(
        Error{QStringLiteral("XML file does not exist at: %1").arg(path)});
  }
  auto contents = readFileBytes(path);
  if (!contents) {
    return std::unexpected(
        Error{QStringLiteral("Failed to read XML file %1: %2")
                  .arg(path, contents.error().message)});
  }
  // Keep whitespace text, comments, and PIs so .text never crosses a node.
  const auto parsed =
      doc.load_buffer(contents->data(), contents->size(),
                      pugi::parse_default | pugi::parse_ws_pcdata |
                          pugi::parse_comments | pugi::parse_pi);
  if (!parsed || !doc.document_element()) {
    return std::unexpected(
        Error{QStringLiteral("Failed to parse XML file %1: %2")
                  .arg(path, QString::fromUtf8(parsed.description()))});
  }
  return {};
}

QString rawText(pugi::xml_node node) {
  QString text;
  for (const auto child : node.children()) {
    if (child.type() != pugi::node_pcdata && child.type() != pugi::node_cdata)
      break;
    text += QString::fromUtf8(child.value());
  }
  return text;
}

glz::json_t elementToJson(pugi::xml_node element) {
  glz::json_t result;
  result.data = glz::json_t::object_t{{element.name(), nodeValue(element)}};
  return result;
}

Result<glz::json_t> xmlFileToJson(const QString& path) {
  pugi::xml_document doc;
  if (auto loaded = loadXmlFile(doc, path); !loaded) {
    return std::unexpected(loaded.error());
  }
  return elementToJson(doc.document_element());
}

}  // namespace px::modrim
