#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/xml.hpp"

using namespace px::modrim;

namespace {

std::string dump(const glz::json_t& value) {
    const auto text = value.dump();
    REQUIRE(text.has_value());
    return *text;
}

}  // namespace

TEST_CASE("elementToJson maps attributes, repeated children, and trimmed text") {
    pugi::xml_document doc;
    REQUIRE(doc.load_string(
        R"(<root a="1"><item>x</item><item>y</item><text>  hi  </text></root>)"));

    CHECK(dump(elementToJson(doc.document_element())) ==
          R"({"root":{"@a":"1","item":["x","y"],"text":"hi"}})");
}

TEST_CASE("childText returns stripped text and treats blank elements as missing") {
    pugi::xml_document doc;
    REQUIRE(doc.load_string("<root><a>  value  </a><b>   </b></root>"));
    const pugi::xml_node root = doc.document_element();

    REQUIRE(childText(root, "a").has_value());
    CHECK(*childText(root, "a") == QStringLiteral("value"));
    CHECK_FALSE(childText(root, "b").has_value());
    CHECK_FALSE(childText(root, "missing").has_value());
}

TEST_CASE("appendJsonAsXml inverts elementToJson") {
    glz::json_t::object_t inner;
    glz::json_t::array_t items;
    glz::json_t first;
    first.data = std::string("a");
    glz::json_t second;
    second.data = std::string("b");
    items.push_back(first);
    items.push_back(second);
    glz::json_t activeMods;
    activeMods.data = glz::json_t::object_t{{"li", glz::json_t{}}};
    std::get<glz::json_t::object_t>(activeMods.data)["li"].data = items;
    glz::json_t version;
    version.data = std::string("1.5");
    inner["version"] = version;
    inner["activeMods"] = activeMods;
    glz::json_t data;
    data.data = glz::json_t::object_t{{"ModsConfigData", glz::json_t{}}};
    std::get<glz::json_t::object_t>(data.data)["ModsConfigData"].data = inner;

    pugi::xml_document doc;
    REQUIRE(appendJsonAsXml(doc, data));
    CHECK(dump(elementToJson(doc.document_element())) == dump(data));
}

TEST_CASE("appendJsonAsXml rejects dictionaries without exactly one root key") {
    glz::json_t data;
    data.data = glz::json_t::object_t{};
    pugi::xml_document doc;
    const auto result = appendJsonAsXml(doc, data);
    REQUIRE_FALSE(result);
    CHECK(result.error().message == QStringLiteral("Dictionary must have exactly one root key"));
}

TEST_CASE("loadXmlFile reports missing files") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    pugi::xml_document doc;
    const auto result = loadXmlFile(doc, QDir(root.path()).filePath(QStringLiteral("nope.xml")));
    REQUIRE_FALSE(result);
    CHECK(result.error().message.startsWith(QStringLiteral("XML file does not exist at")));
}

TEST_CASE("xmlFileToJson reads a file into the legacy dict shape") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString path = QDir(root.path()).filePath(QStringLiteral("ModIds.xml"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("<ModIdsToFix><li>one</li><li>two</li></ModIdsToFix>");
    file.close();

    const auto data = xmlFileToJson(path);
    REQUIRE(data.has_value());
    CHECK(dump(*data) == R"({"ModIdsToFix":{"li":["one","two"]}})");
}
