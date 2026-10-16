#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>
#include <pugixml.hpp>

#include "core/models/metadata/parsing.hpp"
#include "core/models/metadata/structures.hpp"

using px::modrim::AboutXmlMod;
using px::modrim::CaseInsensitiveStr;
using px::modrim::createAboutModFromElement;
using px::modrim::createListedModFromPath;
using px::modrim::ListedMod;

namespace {

// Parses a `<ModMetaData>` document and builds the mod from its root element.
std::shared_ptr<AboutXmlMod> aboutFromXml(const char* xml,
                                          const char* targetVersion = "1.5") {
  pugi::xml_document doc;
  REQUIRE(doc.load_string(xml));
  return createAboutModFromElement(doc.document_element(),
                                   QString::fromUtf8(targetVersion));
}

// Writes `content` as About/About.xml under a fresh mod directory inside
// `root`.
QString writeAboutXml(const QString& root, const QByteArray& content) {
  REQUIRE(QDir(root).mkpath(QStringLiteral("About")));
  QFile file(QDir(root).filePath(QStringLiteral("About/About.xml")));
  REQUIRE(file.open(QIODevice::WriteOnly));
  file.write(content);
  return root;
}

}  // namespace

TEST_CASE("basic About.xml fields are parsed") {
  const auto mod = aboutFromXml(R"(<ModMetaData>
        <name>Test Mod</name>
        <author>Tester</author>
        <packageId>test.mod</packageId>
        <description>A test mod.</description>
        <supportedVersions><li>1.5</li></supportedVersions>
    </ModMetaData>)");

  CHECK(mod->valid);
  CHECK(mod->name == QStringLiteral("Test Mod"));
  CHECK(mod->authors == QStringList{QStringLiteral("Tester")});
  CHECK(mod->packageId.str() == QStringLiteral("test.mod"));
  CHECK(mod->description == QStringLiteral("A test mod."));
  CHECK(mod->supportedVersions == QSet<QString>{QStringLiteral("1.5")});
}

TEST_CASE("About.xml without packageId is invalid") {
  const auto mod =
      aboutFromXml("<ModMetaData><name>No ID Mod</name></ModMetaData>");
  CHECK_FALSE(mod->valid);
  CHECK(mod->packageId.str() == QStringLiteral("missing.packageid"));
}

TEST_CASE("About.xml with empty packageId is invalid") {
  const auto mod =
      aboutFromXml("<ModMetaData><packageId></packageId></ModMetaData>");
  CHECK_FALSE(mod->valid);
  CHECK(mod->packageId.str() == QStringLiteral("missing.packageid"));
}

TEST_CASE("versioned entries must match the version key exactly") {
  const auto wrong = aboutFromXml(R"(<ModMetaData><packageId>x.y</packageId>
        <loadAfterByVersion>
            <v1.50><li>wrong</li></v1.50>
            <v1x5-beta><li>wrong</li></v1x5-beta>
        </loadAfterByVersion>
        <modDependenciesByVersion>
            <v1.50><li><packageId>wrong.dep</packageId></li></v1.50>
        </modDependenciesByVersion>
    </ModMetaData>)");
  CHECK(wrong->aboutRules.loadAfter.size() == 0);
  CHECK(wrong->aboutRules.dependencies.isEmpty());

  const auto right = aboutFromXml(R"(<ModMetaData><packageId>x.y</packageId>
        <loadAfterByVersion><v1.5><li>right</li></v1.5></loadAfterByVersion>
    </ModMetaData>)");
  CHECK(right->aboutRules.loadAfter.contains(QStringLiteral("right")));
}

TEST_CASE("versioned dependencies replace the unversioned list") {
  const auto mod = aboutFromXml(R"(<ModMetaData><packageId>x.y</packageId>
        <modDependencies><li><packageId>plain.dep</packageId></li></modDependencies>
        <modDependenciesByVersion>
            <v1.5><li><packageId>versioned.dep</packageId></li></v1.5>
        </modDependenciesByVersion>
    </ModMetaData>)");

  REQUIRE(mod->aboutRules.dependencies.size() == 1);
  CHECK(mod->aboutRules.dependencies.first().packageId ==
        CaseInsensitiveStr(QStringLiteral("versioned.dep")));
}

TEST_CASE("preferVersioned=false ignores versioned entries") {
  pugi::xml_document doc;
  REQUIRE(doc.load_string(R"(<ModMetaData><packageId>x.y</packageId>
        <modDependenciesByVersion>
            <v1.5><li><packageId>versioned.dep</packageId></li></v1.5>
        </modDependenciesByVersion>
    </ModMetaData>)"));
  const auto mod =
      createAboutModFromElement(doc.document_element(), QStringLiteral("1.5"),
                                /*preferVersioned=*/false);
  CHECK(mod->aboutRules.dependencies.isEmpty());
}

TEST_CASE(
    "dependencies keep declaration order, skip duplicates and null entries") {
  const auto mod = aboutFromXml(R"(<ModMetaData><packageId>x.y</packageId>
        <modDependencies>
            <li><packageId>Dep.One</packageId><displayName>Dep One</displayName>
                <workshopUrl>https://example.test/one</workshopUrl>
                <alternativePackageIds><li>alt.one</li></alternativePackageIds>
            </li>
            <li isNull="True"><packageId>skipped.null</packageId></li>
            <li><packageId>dep.one</packageId><displayName>Duplicate</displayName></li>
            <li><packageId>Dep.Two</packageId></li>
        </modDependencies>
    </ModMetaData>)");

  const auto& deps = mod->aboutRules.dependencies;
  REQUIRE(deps.size() == 2);
  CHECK(deps[0].packageId == CaseInsensitiveStr(QStringLiteral("dep.one")));
  CHECK(deps[0].name == QStringLiteral("Dep One"));
  CHECK(deps[0].workshopUrl == QStringLiteral("https://example.test/one"));
  CHECK(deps[0].alternativePackageIds.contains(QStringLiteral("alt.one")));
  CHECK(deps[1].packageId == CaseInsensitiveStr(QStringLiteral("dep.two")));
}

TEST_CASE("force-load entries are merged into the load-order sets") {
  const auto mod = aboutFromXml(R"(<ModMetaData><packageId>x.y</packageId>
        <loadBefore><li>before.a</li></loadBefore>
        <forceLoadBefore><li>before.b</li></forceLoadBefore>
        <loadAfter><li>after.a</li></loadAfter>
        <forceLoadAfter><li>after.b</li></forceLoadAfter>
        <incompatibleWith><li>bad.one</li></incompatibleWith>
    </ModMetaData>)");

  CHECK(mod->aboutRules.loadBefore.contains(QStringLiteral("before.a")));
  CHECK(mod->aboutRules.loadBefore.contains(QStringLiteral("before.b")));
  CHECK(mod->aboutRules.loadAfter.contains(QStringLiteral("after.a")));
  CHECK(mod->aboutRules.loadAfter.contains(QStringLiteral("after.b")));
  CHECK(mod->aboutRules.incompatibleWith.contains(QStringLiteral("bad.one")));
}

TEST_CASE(
    "descriptionsByVersion selects the description for the target version") {
  const auto mod = aboutFromXml(R"(<ModMetaData><packageId>x.y</packageId>
        <descriptionsByVersion><v1.4>Old text</v1.4><v1.5>Text for 1.5</v1.5></descriptionsByVersion>
    </ModMetaData>)");
  CHECK(mod->description == QStringLiteral("Text for 1.5"));
}

TEST_CASE("a parsed mod without a description does not claim to be invalid") {
  const auto mod = aboutFromXml(
      "<ModMetaData><packageId>x.y</packageId><name>Valid</name></"
      "ModMetaData>");
  CHECK(mod->valid);
  CHECK(mod->description.isEmpty());
}

TEST_CASE("explicit steamAppId is read") {
  const auto mod = aboutFromXml(R"(<ModMetaData><packageId>x.y</packageId>
        <steamAppId>1234</steamAppId></ModMetaData>)");
  CHECK(mod->steamAppId == 1234);
}

TEST_CASE(
    "DLC package ids depend on the RimWorld base game and take the DLC name") {
  const auto mod = aboutFromXml(
      "<ModMetaData><packageId>ludeon.rimworld.royalty</packageId></"
      "ModMetaData>");
  CHECK(mod->valid);
  CHECK(mod->name == QStringLiteral("Royalty"));

  const auto* base = mod->aboutRules.findDependency(
      CaseInsensitiveStr(QStringLiteral("ludeon.rimworld")));
  REQUIRE(base != nullptr);
  CHECK(base->name == QStringLiteral("RimWorld"));
}

TEST_CASE("official mods without a name use the package id mapping") {
  struct Case {
    const char* packageId;
    const char* expectedName;
  };
  const Case cases[] = {
      {"Ludeon.RimWorld", "Core"},
      {"Ludeon.RimWorld.Royalty", "Royalty"},
      {"Ludeon.RimWorld.Ideology", "Ideology"},
      {"Ludeon.RimWorld.Biotech", "Biotech"},
      {"Ludeon.RimWorld.Anomaly", "Anomaly"},
      {"Ludeon.RimWorld.Odyssey", "Odyssey"},
  };
  QTemporaryDir root;
  REQUIRE(root.isValid());

  for (const Case& test : cases) {
    const QString modDir = root.filePath(QString::fromUtf8(test.packageId));
    writeAboutXml(
        modDir, QStringLiteral("<ModMetaData><packageId>%1</packageId>"
                               "<author>Ludeon Studios</author></ModMetaData>")
                    .arg(QString::fromUtf8(test.packageId))
                    .toUtf8());

    const auto result = createListedModFromPath(modDir, QStringLiteral("1.6"));
    REQUIRE(result.has_value());
    CHECK((*result)->valid);
    CHECK((*result)->name == QString::fromUtf8(test.expectedName));
  }
}

TEST_CASE("explicit name wins for official mods") {
  const auto mod = aboutFromXml(R"(<ModMetaData>
        <packageId>ludeon.rimworld.royalty</packageId>
        <name>Custom official name</name></ModMetaData>)",
                                "1.6");
  CHECK(mod->valid);
  CHECK(mod->name == QStringLiteral("Custom official name"));
}

TEST_CASE("createListedModFromPath parses a valid mod directory") {
  QTemporaryDir root;
  REQUIRE(root.isValid());
  const QString modPath =
      writeAboutXml(root.path(), R"(<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Test Mod</name>
  <author>TestAuthor</author>
  <packageId>test.mod</packageId>
  <description>A test mod for unit testing.</description>
  <supportedVersions><li>1.5</li></supportedVersions>
  <modVersion>1.0.0</modVersion>
  <loadAfter><li>ludeon.rimworld</li></loadAfter>
</ModMetaData>
)");

  const auto result = createListedModFromPath(modPath, QStringLiteral("1.5"));
  REQUIRE(result.has_value());
  const auto& mod = *result;
  CHECK(mod->valid);
  const auto* about = dynamic_cast<AboutXmlMod*>(mod.get());
  REQUIRE(about != nullptr);
  CHECK(about->name == QStringLiteral("Test Mod"));
  CHECK(about->authors == QStringList{QStringLiteral("TestAuthor")});
  CHECK(about->packageId.str() == QStringLiteral("test.mod"));
  CHECK(about->modVersion == QStringLiteral("1.0.0"));
  CHECK(
      about->aboutRules.loadAfter.contains(QStringLiteral("ludeon.rimworld")));
  CHECK(mod->modPath() == modPath);
  CHECK(mod->providerId.isEmpty());
  CHECK(about->mtime > 0.0);
}

TEST_CASE("createListedModFromPath rejects non-directory paths") {
  QTemporaryDir root;
  REQUIRE(root.isValid());
  const QString file = writeAboutXml(root.path(), "<ModMetaData/>");

  const auto result = createListedModFromPath(
      QDir(file).filePath(QStringLiteral("About/About.xml")),
      QStringLiteral("1.5"));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().message.startsWith(
      QStringLiteral("Path must be a directory")));
}

TEST_CASE(
    "malformed About.xml yields an invalid AboutXmlMod with the mod path") {
  QTemporaryDir root;
  REQUIRE(root.isValid());
  const QString modPath = writeAboutXml(root.path(), "<ModMetaData>");

  const auto result = createListedModFromPath(modPath, QStringLiteral("1.5"));
  REQUIRE(result.has_value());
  const auto& mod = *result;
  CHECK_FALSE(mod->valid);
  CHECK(dynamic_cast<AboutXmlMod*>(mod.get()) != nullptr);
  CHECK(mod->modPath() == modPath);
  CHECK(mod->uuid() == modPath);
}

TEST_CASE("directory without About.xml yields an invalid plain ListedMod") {
  QTemporaryDir root;
  REQUIRE(root.isValid());

  const auto result =
      createListedModFromPath(root.path(), QStringLiteral("1.5"));
  REQUIRE(result.has_value());
  CHECK_FALSE((*result)->valid);
  CHECK(dynamic_cast<AboutXmlMod*>(result->get()) == nullptr);
}
