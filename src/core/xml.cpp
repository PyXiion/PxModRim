#include "core/xml.hpp"

#include <QFile>
#include <QFileInfo>
#include <string>
#include <type_traits>
#include <utility>

namespace px::modrim {

namespace {

glz::json_t jsonString(std::string value) {
  glz::json_t out;
  out.data = std::move(value);
  return out;
}

QString scalarToString(const glz::json_t& value) {
  return std::visit(
      [](const auto& v) -> QString {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return QString::fromStdString(v);
        } else if constexpr (std::is_same_v<T, bool>) {
          // Python's str(True) spelling, matching the legacy writer.
          return v ? QStringLiteral("True") : QStringLiteral("False");
        } else if constexpr (std::is_same_v<T, double>) {
          return QString::number(v, 'g', 17);
        } else {
          return {};
        }
      },
      value.data);
}

// The value part of `{tag: value}` for one element.
glz::json_t elementValue(pugi::xml_node element) {
  glz::json_t::object_t container;
  bool hasChildren = false;
  for (pugi::xml_node child = element.first_child(); child;
       child = child.next_sibling()) {
    if (child.type() != pugi::node_element) {
      continue;
    }
    hasChildren = true;
    const std::string key = child.name();
    glz::json_t value = elementValue(child);
    auto existing = container.find(key);
    if (existing == container.end()) {
      container.emplace(key, std::move(value));
      continue;
    }
    if (auto* items =
            std::get_if<glz::json_t::array_t>(&existing->second.data)) {
      items->push_back(std::move(value));
    } else {
      glz::json_t::array_t merged;
      merged.push_back(std::move(existing->second));
      merged.push_back(std::move(value));
      existing->second.data = std::move(merged);
    }
  }

  bool hasAttributes = false;
  for (pugi::xml_attribute attribute = element.first_attribute(); attribute;
       attribute = attribute.next_attribute()) {
    hasAttributes = true;
    container["@" + std::string(attribute.name())] =
        jsonString(attribute.value());
  }

  // lxml's `if t.text:` is a truthiness check on the raw text; the stored value
  // is stripped.
  const QString text = rawText(element);
  const QString trimmed = text.trimmed();

  glz::json_t result;
  if (hasChildren || hasAttributes) {
    if (!trimmed.isEmpty()) {
      container["#text"] = jsonString(trimmed.toStdString());
    }
    result.data = std::move(container);
  } else if (!text.isEmpty()) {
    result = jsonString(trimmed.toStdString());
  } else {
    result.data = std::move(container);
  }
  return result;
}

// Fills `element` from a JSON value: objects describe children/attributes/text,
// arrays become anonymous `li` children, and scalars become the element's text.
Result<void> fillElement(pugi::xml_node element, const glz::json_t& value) {
  if (const auto* object = std::get_if<glz::json_t::object_t>(&value.data)) {
    for (const auto& [key, child] : *object) {
      if (key.starts_with('@')) {
        const std::string name = key.substr(1);
        pugi::xml_attribute attribute = element.attribute(name.c_str());
        if (!attribute) {
          attribute = element.append_attribute(name.c_str());
        }
        attribute.set_value(scalarToString(child).toUtf8().constData());
      } else if (key == "#text") {
        element.text().set(scalarToString(child).toUtf8().constData());
      } else if (const auto* items =
                     std::get_if<glz::json_t::array_t>(&child.data)) {
        for (const glz::json_t& item : *items) {
          pugi::xml_node repeated = element.append_child(key.c_str());
          if (auto filled = fillElement(repeated, item); !filled) {
            return filled;
          }
        }
      } else {
        pugi::xml_node single = element.append_child(key.c_str());
        if (auto filled = fillElement(single, child); !filled) {
          return filled;
        }
      }
    }
    return {};
  }
  if (const auto* items = std::get_if<glz::json_t::array_t>(&value.data)) {
    for (const glz::json_t& item : *items) {
      pugi::xml_node li = element.append_child("li");
      if (auto filled = fillElement(li, item); !filled) {
        return filled;
      }
    }
    return {};
  }
  element.text().set(scalarToString(value).toUtf8().constData());
  return {};
}

}  // namespace

Result<void> loadXmlFile(pugi::xml_document& doc, const QString& path) {
  if (!QFileInfo::exists(path)) {
    return std::unexpected(
        Error{QStringLiteral("XML file does not exist at: %1").arg(path)});
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::unexpected(Error{QStringLiteral("Error parsing XML file %1: %2")
                                     .arg(path, file.errorString())});
  }
  const QByteArray bytes = file.readAll();

  const pugi::xml_parse_result parsed =
      doc.load_buffer(bytes.constData(), bytes.size());
  if (!parsed) {
    return std::unexpected(
        Error{QStringLiteral("Error parsing XML file %1: %2")
                  .arg(path, QString::fromUtf8(parsed.description()))});
  }
  if (!doc.document_element()) {
    return std::unexpected(
        Error{QStringLiteral("Error parsing XML file %1: no root element")
                  .arg(path)});
  }
  return {};
}

QString rawText(pugi::xml_node node) {
  QString text;
  for (pugi::xml_node child = node.first_child(); child;
       child = child.next_sibling()) {
    if (child.type() != pugi::node_pcdata && child.type() != pugi::node_cdata) {
      break;
    }
    text += QString::fromUtf8(child.value());
  }
  return text;
}

std::optional<QString> childText(pugi::xml_node parent, const char* tag) {
  const pugi::xml_node found = parent.child(tag);
  if (!found) {
    return std::nullopt;
  }
  const QString text = rawText(found).trimmed();
  if (text.isEmpty()) {
    return std::nullopt;
  }
  return text;
}

glz::json_t elementToJson(pugi::xml_node element) {
  glz::json_t::object_t wrapped;
  wrapped.emplace(std::string(element.name()), elementValue(element));
  glz::json_t out;
  out.data = std::move(wrapped);
  return out;
}

Result<glz::json_t> xmlFileToJson(const QString& path) {
  pugi::xml_document doc;
  if (auto loaded = loadXmlFile(doc, path); !loaded) {
    return std::unexpected(loaded.error());
  }
  return elementToJson(doc.document_element());
}

Result<void> appendJsonAsXml(pugi::xml_node parent, const glz::json_t& data) {
  const auto* object = std::get_if<glz::json_t::object_t>(&data.data);
  if (object == nullptr || object->size() != 1) {
    return std::unexpected(
        Error{QStringLiteral("Dictionary must have exactly one root key")});
  }
  const auto& [rootKey, rootValue] = *object->begin();
  pugi::xml_node root = parent.append_child(rootKey.c_str());
  return fillElement(root, rootValue);
}

}  // namespace px::modrim
