#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include "core/xml.hpp"

using namespace px::modrim;

namespace {

std::string dump(const glz::json_t& value) {
  const auto text = value.dump();
  REQUIRE(text.has_value());
  return *text;
}

}  // namespace

TEST_CASE(
    "elementToJson maps attributes, repeated children, and trimmed text") {
  pugi::xml_document doc;
  REQUIRE(doc.load_string(
      R"(<root a="1"><item>x</item><item>y</item><text>  hi  </text></root>)"));

  CHECK(dump(elementToJson(doc.document_element())) ==
        R"({"root":{"@a":"1","item":["x","y"],"text":"hi"}})");
}

TEST_CASE("loadXmlFile reports missing files") {
  QTemporaryDir root;
  REQUIRE(root.isValid());
  pugi::xml_document doc;
  const auto result =
      loadXmlFile(doc, QDir(root.path()).filePath(QStringLiteral("nope.xml")));
  REQUIRE_FALSE(result);
  CHECK(result.error().message.startsWith(
      QStringLiteral("XML file does not exist at")));
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
