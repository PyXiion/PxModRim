#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <pugixml.hpp>

#include "core/file_io.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/mods_config.hpp"
#include "test_support.hpp"

namespace {

using namespace px::modrim;
using test_support::readBytes;
using test_support::writeBytes;

constexpr auto kSampleXml = R"(<?xml version="1.0" encoding="utf-8"?>
<ModsConfigData>
  <version>1.6.4871 rev600</version>
  <activeMods>
    <li>ludeon.rimworld</li>
    <li>ludeon.rimworld.royalty</li>
    <li>ludeon.rimworld.ideology</li>
    <li>ludeon.rimworld.biotech</li>
    <li>ludeon.rimworld.anomaly</li>
    <li>ludeon.rimworld.odyssey</li>
  </activeMods>
  <knownExpansions>
    <li>ludeon.rimworld.royalty</li>
    <li>ludeon.rimworld.ideology</li>
    <li>ludeon.rimworld.biotech</li>
    <li>ludeon.rimworld.anomaly</li>
    <li>ludeon.rimworld.odyssey</li>
  </knownExpansions>
</ModsConfigData>
)";

CaseInsensitiveStr pid(const char* text) {
  return CaseInsensitiveStr(QString::fromLatin1(text));
}

std::optional<ModsConfig> parseText(const std::string& content,
                                    const QTemporaryDir& dir) {
  const QString path = dir.filePath(QStringLiteral("ModsConfig.xml"));
  REQUIRE(writeBytes(path, content));
  return parseModsConfig(path);
}

ModsConfig modsConfig(const char* packageId) {
  return ModsConfig{
      .version = QStringLiteral("1.5"),
      .activeMods = {pid(packageId)},
      .knownExpansions = {},
  };
}

// Package ids of the snapshots in `dir`, newest first.
QList<CaseInsensitiveStr> retainedIds(const QString& dir) {
  QList<CaseInsensitiveStr> ids;
  for (const auto& snapshot : getSnapshots(dir)) {
    const auto parsed = parseModsConfig(snapshot);
    REQUIRE(parsed.has_value());
    ids.append(parsed->activeMods.first());
  }
  return ids;
}

}  // namespace

TEST_CASE("parse_mods_config reads version, active mods and expansions") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto config = parseText(kSampleXml, dir);
  REQUIRE(config.has_value());
  REQUIRE(config->version == QStringLiteral("1.6.4871 rev600"));
  REQUIRE(config->activeMods == QList<CaseInsensitiveStr>{
                                    pid("ludeon.rimworld"),
                                    pid("ludeon.rimworld.royalty"),
                                    pid("ludeon.rimworld.ideology"),
                                    pid("ludeon.rimworld.biotech"),
                                    pid("ludeon.rimworld.anomaly"),
                                    pid("ludeon.rimworld.odyssey"),
                                });
  REQUIRE(config->knownExpansions == QList<CaseInsensitiveStr>{
                                         pid("ludeon.rimworld.royalty"),
                                         pid("ludeon.rimworld.ideology"),
                                         pid("ludeon.rimworld.biotech"),
                                         pid("ludeon.rimworld.anomaly"),
                                         pid("ludeon.rimworld.odyssey"),
                                     });
}

TEST_CASE("parse_mods_config accepts a single active mod") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto config = parseText(R"(<?xml version="1.0" encoding="utf-8"?>
<ModsConfigData>
  <version>1.5</version>
  <activeMods>
    <li>ludeon.rimworld</li>
  </activeMods>
  <knownExpansions>
    <li>ludeon.rimworld.royalty</li>
  </knownExpansions>
</ModsConfigData>)",
                                dir);
  REQUIRE(config.has_value());
  REQUIRE(config->activeMods ==
          QList<CaseInsensitiveStr>{pid("ludeon.rimworld")});
  REQUIRE(config->knownExpansions ==
          QList<CaseInsensitiveStr>{pid("ludeon.rimworld.royalty")});
}

TEST_CASE(
    "parse_mods_config falls back to the DLC list when no expansions are "
    "declared") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto config = parseText(R"(<?xml version="1.0" encoding="utf-8"?>
<ModsConfigData>
  <version>1.5</version>
  <activeMods/>
</ModsConfigData>)",
                                dir);
  REQUIRE(config.has_value());
  REQUIRE(config->activeMods.isEmpty());
  REQUIRE_FALSE(config->knownExpansions.isEmpty());
}

TEST_CASE("parse_mods_config returns nullopt for a missing file") {
  REQUIRE_FALSE(parseModsConfig(QStringLiteral("/nonexistent/ModsConfig.xml"))
                    .has_value());
}

TEST_CASE("write then parse round-trips the mod lists") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const ModsConfig original{
      .version = QStringLiteral("1.6.4871 rev600"),
      .activeMods =
          {
              pid("ludeon.rimworld"),
              pid("ludeon.rimworld.royalty"),
              pid("ludeon.rimworld.ideology"),
              pid("ludeon.rimworld.biotech"),
              pid("ludeon.rimworld.anomaly"),
          },
      .knownExpansions =
          {
              pid("ludeon.rimworld.royalty"),
              pid("ludeon.rimworld.ideology"),
              pid("ludeon.rimworld.biotech"),
              pid("ludeon.rimworld.anomaly"),
          },
  };
  const QString path = dir.filePath(QStringLiteral("ModsConfig.xml"));
  REQUIRE(
      writeModsConfig(path, original, dir.filePath(QStringLiteral("snapshots")))
          .has_value());

  const auto loaded = parseModsConfig(path);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->version == original.version);
  REQUIRE(loaded->activeMods == original.activeMods);
  REQUIRE(loaded->knownExpansions == original.knownExpansions);
}

TEST_CASE("write keeps unknown XML in an existing ModsConfigData document") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("ModsConfig.xml"));
  std::string text = kSampleXml;
  text.replace(
      text.find("<ModsConfigData>"), std::string("<ModsConfigData>").size(),
      R"(<ModsConfigData custom="preserve"><otherSetting enabled="true"><value>42</value></otherSetting>)");
  REQUIRE(writeBytes(path, text));

  auto loaded = parseModsConfig(path);
  REQUIRE(loaded.has_value());
  loaded->activeMods = {pid("author.updated")};
  REQUIRE(
      writeModsConfig(path, *loaded, dir.filePath(QStringLiteral("snapshots")))
          .has_value());

  pugi::xml_document doc;
  REQUIRE(doc.load_file(path.toStdString().c_str()));
  const pugi::xml_node root = doc.document_element();
  REQUIRE(std::string(root.attribute("custom").value()) == "preserve");
  REQUIRE(std::string(root.child("otherSetting").child_value("value")) == "42");
  REQUIRE(
      std::string(root.child("otherSetting").attribute("enabled").value()) ==
      "true");
  REQUIRE(std::string(root.child("activeMods").child_value("li")) ==
          "author.updated");
}

#ifndef Q_OS_WIN
TEST_CASE("snapshot discovery and pruning match Python's POSIX glob") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString snapshotsDir = dir.filePath(QStringLiteral("snapshots"));
  REQUIRE(QDir().mkpath(snapshotsDir));
  const QString pythonSnapshot = QDir(snapshotsDir).filePath(
      QStringLiteral("ModsConfig_20260922_100000_123456_001.xml"));
  const QString lowerPrefix = QDir(snapshotsDir).filePath(
      QStringLiteral("modsconfig_20260922_100001_123456.xml"));
  const QString upperSuffix = QDir(snapshotsDir).filePath(
      QStringLiteral("ModsConfig_20260922_100002_123456.XML"));
  const QString snapshotDirectory = QDir(snapshotsDir).filePath(
      QStringLiteral("ModsConfig_20260922_100003_123456.xml"));
  REQUIRE(writeBytes(pythonSnapshot, kSampleXml));
  REQUIRE(writeBytes(lowerPrefix, kSampleXml));
  REQUIRE(writeBytes(upperSuffix, kSampleXml));
  REQUIRE(QDir().mkpath(snapshotDirectory));

  REQUIRE(getSnapshots(snapshotsDir) == QStringList{pythonSnapshot});
  const auto parsed = parseModsConfig(pythonSnapshot);
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->version == QStringLiteral("1.6.4871 rev600"));

  const QString configPath = dir.filePath(QStringLiteral("ModsConfig.xml"));
  REQUIRE(writeBytes(configPath,
                     "<ModsConfigData><version>1.6</version>"
                     "<activeMods><li>mod.current</li></activeMods>"
                     "</ModsConfigData>"));
  const auto created = createSnapshot(configPath, snapshotsDir, 1);
  REQUIRE(created.has_value());
  REQUIRE(created->has_value());
  REQUIRE(getSnapshots(snapshotsDir) == QStringList{**created});
  REQUIRE_FALSE(QFileInfo::exists(pythonSnapshot));
  REQUIRE(readBytes(lowerPrefix) == kSampleXml);
  REQUIRE(readBytes(upperSuffix) == kSampleXml);
  REQUIRE(QFileInfo(snapshotDirectory).isDir());
}
#endif

TEST_CASE("replacing a config keeps the previous contents as a snapshot") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString snapshotsDir = dir.filePath(QStringLiteral("snapshots"));
  const QString configPath = dir.filePath(QStringLiteral("ModsConfig.xml"));

  REQUIRE(writeModsConfig(configPath, modsConfig("mod.alpha"), snapshotsDir)
              .has_value());
  REQUIRE(getSnapshots(snapshotsDir).isEmpty());

  REQUIRE(writeModsConfig(configPath, modsConfig("mod.beta"), snapshotsDir)
              .has_value());

  const auto snapshots = getSnapshots(snapshotsDir);
  REQUIRE(snapshots.size() == 1);
  const auto previous = parseModsConfig(snapshots.first());
  const auto current = parseModsConfig(configPath);
  REQUIRE(previous.has_value());
  REQUIRE(previous->activeMods == QList<CaseInsensitiveStr>{pid("mod.alpha")});
  REQUIRE(current.has_value());
  REQUIRE(current->activeMods == QList<CaseInsensitiveStr>{pid("mod.beta")});
}

TEST_CASE("replacement retains only the newest ten snapshots") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString snapshotsDir = dir.filePath(QStringLiteral("snapshots"));
  const QString configPath = dir.filePath(QStringLiteral("ModsConfig.xml"));
  REQUIRE(writeModsConfig(configPath, modsConfig("mod.0"), snapshotsDir)
              .has_value());

  for (int index = 1; index < 12; ++index) {
    const std::string id = "mod." + std::to_string(index);
    REQUIRE(writeModsConfig(configPath, modsConfig(id.c_str()), snapshotsDir)
                .has_value());
  }

  QList<CaseInsensitiveStr> expected;
  for (int index = 10; index >= 1; --index) {
    expected.append(pid(("mod." + std::to_string(index)).c_str()));
  }
  REQUIRE(getSnapshots(snapshotsDir).size() == 10);
  REQUIRE(retainedIds(snapshotsDir) == expected);
}

TEST_CASE("configurable retention limit is honored") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString snapshotsDir = dir.filePath(QStringLiteral("snapshots"));
  const QString configPath = dir.filePath(QStringLiteral("ModsConfig.xml"));
  REQUIRE(writeModsConfig(configPath, modsConfig("mod.0"), snapshotsDir, 3)
              .has_value());

  for (int index = 1; index < 6; ++index) {
    const std::string id = "mod." + std::to_string(index);
    REQUIRE(writeModsConfig(configPath, modsConfig(id.c_str()), snapshotsDir, 3)
                .has_value());
  }

  const auto snapshots = getSnapshots(snapshotsDir);
  REQUIRE(snapshots.size() == 3);
  REQUIRE(retainedIds(snapshotsDir) ==
          QList<CaseInsensitiveStr>{pid("mod.4"), pid("mod.3"), pid("mod.2")});

  // Lowering the limit on duplicate content prunes the excess before returning.
  REQUIRE(writeBytes(configPath, readBytes(snapshots.first())));
  const auto duplicate = createSnapshot(configPath, snapshotsDir, 2);
  REQUIRE(duplicate.has_value());
  REQUIRE(duplicate->has_value());
  REQUIRE(**duplicate == snapshots.first());
  REQUIRE(getSnapshots(snapshotsDir).size() == 2);
}

TEST_CASE(
    "restoring the oldest snapshot at full retention keeps it and backs up the "
    "current file") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString snapshotsDir = dir.filePath(QStringLiteral("snapshots"));
  const QString configPath = dir.filePath(QStringLiteral("ModsConfig.xml"));
  constexpr int kMaxSnapshots = 3;
  REQUIRE(writeModsConfig(configPath, modsConfig("mod.0"), snapshotsDir,
                          kMaxSnapshots)
              .has_value());
  for (int index = 1; index < 4; ++index) {
    const std::string id = "mod." + std::to_string(index);
    REQUIRE(writeModsConfig(configPath, modsConfig(id.c_str()), snapshotsDir,
                            kMaxSnapshots)
                .has_value());
  }

  const QString selected = getSnapshots(snapshotsDir).last();
  const std::string selectedData = readBytes(selected);

  REQUIRE(restoreSnapshot(selected, configPath, snapshotsDir, kMaxSnapshots)
              .has_value());

  const auto restored = parseModsConfig(configPath);
  REQUIRE(restored.has_value());
  REQUIRE(restored->activeMods == QList<CaseInsensitiveStr>{pid("mod.0")});
  REQUIRE(QFileInfo::exists(selected));
  REQUIRE(readBytes(selected) == selectedData);
  REQUIRE(getSnapshots(snapshotsDir).size() == kMaxSnapshots);
  REQUIRE(retainedIds(snapshotsDir) ==
          QList<CaseInsensitiveStr>{pid("mod.3"), pid("mod.2"), pid("mod.0")});
}

TEST_CASE(
    "restoring a snapshot writes its exact bytes and backs up the current "
    "file") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString snapshotsDir = dir.filePath(QStringLiteral("snapshots"));
  const QString configPath = dir.filePath(QStringLiteral("ModsConfig.xml"));
  REQUIRE(writeModsConfig(configPath, modsConfig("mod.current"), snapshotsDir)
              .has_value());

  const QString snapshotPath =
      dir.filePath(QStringLiteral("snapshots/ModsConfig_20260922_100000.xml"));
  const std::string snapshotData =
      "<?xml version='1.0' encoding='utf-8'?>\n"
      "<ModsConfigData><version>1.5</version><activeMods>"
      "<li>mod.saved</li></activeMods></ModsConfigData>\n";
  REQUIRE(QDir().mkpath(dir.filePath(QStringLiteral("snapshots"))));
  REQUIRE(writeBytes(snapshotPath, snapshotData));

  REQUIRE(restoreSnapshot(snapshotPath, configPath, snapshotsDir).has_value());
  REQUIRE(readBytes(configPath) == snapshotData);

  QStringList backups;
  for (const auto& snapshot : getSnapshots(snapshotsDir)) {
    if (QFileInfo(snapshot) != QFileInfo(snapshotPath)) {
      backups.append(snapshot);
    }
  }
  REQUIRE(backups.size() == 1);
  const auto backup = parseModsConfig(backups.first());
  REQUIRE(backup.has_value());
  REQUIRE(backup->activeMods == QList<CaseInsensitiveStr>{pid("mod.current")});
}

TEST_CASE("invalid snapshots never change the current file") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString snapshotsDir = dir.filePath(QStringLiteral("snapshots"));
  const QString configPath = dir.filePath(QStringLiteral("ModsConfig.xml"));
  REQUIRE(writeModsConfig(configPath, modsConfig("mod.safe"), snapshotsDir)
              .has_value());
  const std::string original = readBytes(configPath);

  const QString missing =
      dir.filePath(QStringLiteral("snapshots/ModsConfig_missing.xml"));
  const QString corrupt =
      dir.filePath(QStringLiteral("snapshots/ModsConfig_corrupt.xml"));
  const QString wrongRoot =
      dir.filePath(QStringLiteral("snapshots/ModsConfig_wrong_root.xml"));
  const QString missingActive = dir.filePath(
      QStringLiteral("snapshots/ModsConfig_missing_active_mods.xml"));
  REQUIRE(QDir().mkpath(snapshotsDir));
  REQUIRE(writeBytes(corrupt, "not XML"));
  REQUIRE(writeBytes(wrongRoot, "<NotModsConfig/>"));
  REQUIRE(
      writeBytes(missingActive,
                 "<ModsConfigData><version>1.5</version></ModsConfigData>"));

  for (const QString& snapshot : {missing, corrupt, wrongRoot, missingActive}) {
    const QStringList before = getSnapshots(snapshotsDir);
    REQUIRE_FALSE(
        restoreSnapshot(snapshot, configPath, snapshotsDir).has_value());
    REQUIRE(readBytes(configPath) == original);
    REQUIRE(getSnapshots(snapshotsDir) == before);
  }
}

TEST_CASE(
    "a failed replacement reports an error and leaves no temporary files") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString snapshotsDir = dir.filePath(QStringLiteral("snapshots"));
  const QString configPath = dir.filePath(QStringLiteral("ModsConfig.xml"));
  REQUIRE(writeModsConfig(configPath, modsConfig("mod.safe"), snapshotsDir)
              .has_value());

  // A directory at the target makes the final rename fail after the temporary
  // file is written.
  const QString blocked = dir.filePath(QStringLiteral("Blocked.xml"));
  REQUIRE(QDir().mkpath(blocked));
  REQUIRE_FALSE(writeModsConfig(blocked, modsConfig("mod.new"), snapshotsDir)
                    .has_value());
  REQUIRE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden) ==
          QStringList{QStringLiteral("ModsConfig.xml")});
}
