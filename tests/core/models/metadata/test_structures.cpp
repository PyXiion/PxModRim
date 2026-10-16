#include <QHash>
#include <QSet>

#include <catch2/catch_test_macros.hpp>

#include "core/models/metadata/structures.hpp"

using px::modrim::AboutXmlMod;
using px::modrim::CaseInsensitiveSet;
using px::modrim::CaseInsensitiveStr;
using px::modrim::ListedMod;
using px::modrim::ModsConfig;

TEST_CASE("CaseInsensitiveStr stores the lowercased package id") {
  const CaseInsensitiveStr pid(QStringLiteral("Test.Mod.PackageId"));
  CHECK(pid.str() == QStringLiteral("test.mod.packageid"));
}

TEST_CASE("CaseInsensitiveStr compares ignoring case") {
  const CaseInsensitiveStr a(QStringLiteral("Test.Mod"));
  const CaseInsensitiveStr b(QStringLiteral("test.mod"));
  CHECK(a == b);
}

TEST_CASE("CaseInsensitiveStr finds hash entries regardless of case") {
  QHash<CaseInsensitiveStr, QString> map;
  map.insert(CaseInsensitiveStr(QStringLiteral("Test.Mod")),
             QStringLiteral("value"));
  CHECK(map.value(CaseInsensitiveStr(QStringLiteral("test.mod"))) ==
        QStringLiteral("value"));
}

TEST_CASE("CaseInsensitiveSet membership ignores case") {
  CaseInsensitiveSet set;
  set.add(QStringLiteral("Test.Mod"));
  CHECK(set.contains(QStringLiteral("test.mod")));
  CHECK(set.contains(QStringLiteral("Test.Mod")));
}

TEST_CASE("CaseInsensitiveSet discard removes regardless of case") {
  CaseInsensitiveSet set(QStringList{QStringLiteral("Test.Mod")});
  set.discard(QStringLiteral("test.mod"));
  CHECK(set.size() == 0);
}

TEST_CASE("CaseInsensitiveSet union keeps elements of both operands") {
  const CaseInsensitiveSet a(QStringList{QStringLiteral("mod.a")});
  const CaseInsensitiveSet b(QStringList{QStringLiteral("Mod.B")});
  const CaseInsensitiveSet c = a | b;
  CHECK(c.contains(QStringLiteral("mod.a")));
  CHECK(c.contains(QStringLiteral("mod.b")));
}

TEST_CASE("empty CaseInsensitiveSets compare equal") {
  CHECK(CaseInsensitiveSet() == CaseInsensitiveSet());
}

TEST_CASE("ListedMod has the invalid-mod defaults") {
  const ListedMod mod;
  CHECK(mod.name == QStringLiteral("Unknown Mod Name"));
  CHECK(mod.valid);
  CHECK(mod.providerId.isEmpty());
  CHECK_FALSE(mod.modPath().has_value());
}

TEST_CASE("ListedMod provider_id is assignable") {
  ListedMod mod;
  mod.providerId = QStringLiteral("local");
  CHECK(mod.providerId == QStringLiteral("local"));
}

TEST_CASE("pathless ListedMod uuids are unique") {
  const ListedMod a;
  const ListedMod b;
  CHECK(a.uuid() != b.uuid());
}

TEST_CASE("ListedMod mod_path can be set only once") {
  ListedMod mod;
  REQUIRE(mod.setModPath(QStringLiteral("/mods/a")));
  CHECK(mod.uuid() == QStringLiteral("/mods/a"));

  const auto second = mod.setModPath(QStringLiteral("/mods/b"));
  REQUIRE_FALSE(second);
  CHECK(second.error().message ==
        QStringLiteral("Mod path already set. Cannot override."));
  CHECK(mod.modPath() == QStringLiteral("/mods/a"));
}

TEST_CASE("AboutXmlMod has the About.xml defaults") {
  const AboutXmlMod mod;
  CHECK(mod.name == QStringLiteral("Unknown Mod Name"));
  CHECK(mod.authors.isEmpty());
  CHECK(mod.modVersion.isEmpty());
  CHECK(mod.packageId == CaseInsensitiveStr(QStringLiteral("invalid.mod")));
}

TEST_CASE("AboutXmlMod reports the DLC name for DLC package ids") {
  AboutXmlMod mod;
  mod.packageId = CaseInsensitiveStr(QStringLiteral("ludeon.rimworld.royalty"));
  REQUIRE(mod.getDlcName().has_value());
  CHECK(*mod.getDlcName() == QStringLiteral("RimWorld - Royalty"));
}

TEST_CASE("ModsConfig normalizes package ids to lowercase") {
  const ModsConfig config{
      .version = QStringLiteral("1.5"),
      .activeMods = {CaseInsensitiveStr(QStringLiteral("Test.UPPERCASE.Mod"))},
      .knownExpansions = {},
  };
  CHECK(config.activeMods.first() ==
        CaseInsensitiveStr(QStringLiteral("test.uppercase.mod")));
}

TEST_CASE("ModsConfig clear_active_mods keeps expansions") {
  ModsConfig config{
      .version = QStringLiteral("1.5"),
      .activeMods = {CaseInsensitiveStr(QStringLiteral("ludeon.rimworld"))},
      .knownExpansions = {CaseInsensitiveStr(
          QStringLiteral("ludeon.rimworld.royalty"))},
  };
  config.clearActiveMods();
  CHECK(config.activeMods.isEmpty());
  CHECK(config.knownExpansions.size() == 1);
}

TEST_CASE("ModsConfig clear_all empties both lists") {
  ModsConfig config{
      .version = QStringLiteral("1.5"),
      .activeMods = {CaseInsensitiveStr(QStringLiteral("ludeon.rimworld"))},
      .knownExpansions = {CaseInsensitiveStr(
          QStringLiteral("ludeon.rimworld.royalty"))},
  };
  config.clearAll();
  CHECK(config.activeMods.isEmpty());
  CHECK(config.knownExpansions.isEmpty());
}

TEST_CASE("ModsConfig detects duplicate active mods ignoring case") {
  const ModsConfig duplicated{
      .version = QStringLiteral("1.5"),
      .activeMods = {CaseInsensitiveStr(QStringLiteral("mod.a")),
                     CaseInsensitiveStr(QStringLiteral("Mod.A"))},
      .knownExpansions = {},
  };
  CHECK(duplicated.checkActiveDuplicates());

  const ModsConfig distinct{
      .version = QStringLiteral("1.5"),
      .activeMods = {CaseInsensitiveStr(QStringLiteral("mod.a")),
                     CaseInsensitiveStr(QStringLiteral("mod.b"))},
      .knownExpansions = {},
  };
  CHECK_FALSE(distinct.checkActiveDuplicates());
}

TEST_CASE("ModsConfig detects duplicate expansions ignoring case") {
  const ModsConfig duplicated{
      .version = QStringLiteral("1.5"),
      .activeMods = {},
      .knownExpansions =
          {CaseInsensitiveStr(QStringLiteral("ludeon.rimworld.royalty")),
           CaseInsensitiveStr(QStringLiteral("Ludeon.RimWorld.Royalty"))},
  };
  CHECK(duplicated.checkExpansionsDuplicates());

  const ModsConfig distinct{
      .version = QStringLiteral("1.5"),
      .activeMods = {},
      .knownExpansions =
          {CaseInsensitiveStr(QStringLiteral("ludeon.rimworld.royalty")),
           CaseInsensitiveStr(QStringLiteral("ludeon.rimworld.ideology"))},
  };
  CHECK_FALSE(distinct.checkExpansionsDuplicates());
}
