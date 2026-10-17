#include "core/xml.hpp"

#include <QFile>
#include <QFileInfo>
#include <string>
#include <utility>
#include <variant>

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

std::string scalarText(const glz::json_t& value) {
  if (const auto* text = std::get_if<std::string>(&value.data)) return *text;
  if (const auto* boolean = std::get_if<bool>(&value.data)) {
    return *boolean ? "True" : "False";
  }
  if (std::holds_alternative<std::nullptr_t>(value.data)) return "None";
  return value.dump().value_or(std::string{});
}

void populateNode(pugi::xml_node node, const glz::json_t& value) {
  if (const auto* fields = std::get_if<glz::json_t::object_t>(&value.data)) {
    for (const auto& [key, item] : *fields) {
      if (key.starts_with('@')) {
        node.append_attribute(key.c_str() + 1)
            .set_value(scalarText(item).c_str());
      } else if (key == "#text") {
        // A leading text node must remain before children regardless of map
        // order.
        auto first = node.first_child();
        if (first.type() != pugi::node_pcdata &&
            first.type() != pugi::node_cdata) {
          first = node.prepend_child(pugi::node_pcdata);
        }
        first.set_value(scalarText(item).c_str());
      } else if (const auto* entries =
                     std::get_if<glz::json_t::array_t>(&item.data)) {
        for (const auto& entry : *entries) {
          populateNode(node.append_child(key.c_str()), entry);
        }
      } else {
        populateNode(node.append_child(key.c_str()), item);
      }
    }
  } else if (const auto* entries =
                 std::get_if<glz::json_t::array_t>(&value.data)) {
    for (const auto& entry : *entries) {
      populateNode(node.append_child("li"), entry);
    }
  } else {
    node.append_child(pugi::node_pcdata).set_value(scalarText(value).c_str());
  }
}

}  // namespace

Result<void> loadXmlFile(pugi::xml_document& doc, const QString& path) {
  if (!QFileInfo::exists(path)) {
    return std::unexpected(
        Error{QStringLiteral("XML file does not exist at: %1").arg(path)});
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::unexpected(
        Error{QStringLiteral("Failed to read XML file %1: %2")
                  .arg(path, file.errorString())});
  }
  const QByteArray contents = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    return std::unexpected(
        Error{QStringLiteral("Failed to read XML file %1: %2")
                  .arg(path, file.errorString())});
  }
  // Keep whitespace text, comments, and PIs so .text never crosses a node.
  const auto parsed = doc.load_buffer(
      contents.constData(), static_cast<std::size_t>(contents.size()),
      pugi::parse_default | pugi::parse_ws_pcdata | pugi::parse_comments |
          pugi::parse_pi);
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

std::optional<QString> childText(pugi::xml_node parent, const char* tag) {
  const QString text = rawText(parent.child(tag)).trimmed();
  if (text.isEmpty()) return std::nullopt;
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

Result<void> appendJsonAsXml(pugi::xml_node parent, const glz::json_t& data) {
  const auto* roots = std::get_if<glz::json_t::object_t>(&data.data);
  if (!roots || roots->size() != 1) {
    return std::unexpected(
        Error{QStringLiteral("Dictionary must have exactly one root key")});
  }
  const auto& [name, value] = *roots->begin();
  populateNode(parent.append_child(name.c_str()), value);
  return {};
}

}  // namespace px::modrim
