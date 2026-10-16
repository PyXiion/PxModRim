#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <glaze/glaze.hpp>

#include "core/error.hpp"

namespace px::modrim {

// Package id that compares and hashes without regard to case. Stored
// lowercased.
class CaseInsensitiveStr {
 public:
  CaseInsensitiveStr() = default;
  explicit CaseInsensitiveStr(QStringView pid)
      : m_value(pid.toString().toLower()) {}

  const QString& str() const noexcept { return m_value; }

  friend bool operator==(const CaseInsensitiveStr&,
                         const CaseInsensitiveStr&) = default;
  friend size_t qHash(const CaseInsensitiveStr& key, size_t seed = 0) noexcept {
    return qHash(key.m_value, seed);
  }

 private:
  QString m_value;
};

// Set of CaseInsensitiveStr with string-friendly membership tests.
class CaseInsensitiveSet {
 public:
  CaseInsensitiveSet() = default;
  explicit CaseInsensitiveSet(const QStringList& values);
  explicit CaseInsensitiveSet(const QList<CaseInsensitiveStr>& values);

  bool contains(QStringView value) const;
  bool contains(const CaseInsensitiveStr& value) const;
  void add(QStringView value);
  void add(const CaseInsensitiveStr& value);
  void discard(QStringView value);
  void discard(const CaseInsensitiveStr& value);

  qsizetype size() const noexcept { return m_data.size(); }
  bool empty() const noexcept { return m_data.isEmpty(); }

  // Set union; the result keeps the elements of both operands.
  CaseInsensitiveSet operator|(const CaseInsensitiveSet& other) const;

  auto begin() const { return m_data.cbegin(); }
  auto end() const { return m_data.cend(); }

  friend bool operator==(const CaseInsensitiveSet&,
                         const CaseInsensitiveSet&) = default;

 private:
  QSet<CaseInsensitiveStr> m_data;
};

// Parsed ModsConfig.xml data with active mods and known expansions.
struct ModsConfig {
  QString version;
  QList<CaseInsensitiveStr> activeMods;
  QList<CaseInsensitiveStr> knownExpansions;

  bool checkActiveDuplicates() const;
  bool checkExpansionsDuplicates() const;
  void clearActiveMods() { activeMods.clear(); }
  void clearAll() {
    activeMods.clear();
    knownExpansions.clear();
  }
};

struct ReplacementInfo {
  QString name;
  QString author;
  QString packageid;
  QString pfid;
  QStringList supportedversions;
  QString source = QStringLiteral("database");
};

// Dependency declared by a mod's About.xml (or synthesized, e.g. the RimWorld
// base game for DLC).
struct DependencyMod {
  QString name;
  CaseInsensitiveStr packageId{QStringLiteral("invalid.mod")};
  QString workshopUrl;
  CaseInsensitiveSet alternativePackageIds;
};

// Load-order rules and dependencies. Dependencies keep declaration order.
struct BaseRules {
  CaseInsensitiveSet loadAfter;
  CaseInsensitiveSet loadBefore;
  CaseInsensitiveSet incompatibleWith;
  QList<DependencyMod> dependencies;

  const DependencyMod* findDependency(
      const CaseInsensitiveStr& packageId) const;
  bool hasDependency(const CaseInsensitiveStr& packageId) const {
    return findDependency(packageId) != nullptr;
  }
  // Inserts, or replaces the dependency with the same package id in place.
  void setDependency(DependencyMod dependency);
};

// Mod discovered by any provider. Polymorphic so providers can hand out
// AboutXmlMod (and ScenarioMod) through a shared base pointer.
class ListedMod {
 public:
  ListedMod() = default;
  ListedMod(const ListedMod&) = default;
  ListedMod& operator=(const ListedMod&) = default;
  virtual ~ListedMod() = default;

  QString name = QStringLiteral("Unknown Mod Name");
  bool valid = true;
  QSet<QString> supportedVersions;
  QString description = QStringLiteral(
      "This mod is considered invalid by PxModRim (and the RimWorld game).\n\n"
      "This mod does NOT contain an ./About/About.xml and is likely leftover "
      "from previous "
      "usage.");
  QString providerId;
  bool obsolete = false;
  bool dbBuilderNoName = false;

  // Identity: the mod path once it is known, otherwise a random id assigned at
  // construction.
  virtual QString uuid() const;

  const std::optional<QString>& modPath() const noexcept { return m_modPath; }
  // Fails if a path was already set; a mod's directory is fixed after
  // discovery.
  Result<void> setModPath(const QString& path);

  // Folder name without its last extension (Python's Path.stem); nullopt
  // without a path.
  std::optional<QString> modFolder() const;
  // Modification time of the mod folder in seconds since the epoch, or -1 if
  // unavailable.
  qint64 internalTimeTouched() const;

  // Published file id from About/PublishedFileId.txt or a numeric folder name.
  // Cached on first use; this touches the disk, so callers may warm it off the
  // UI thread.
  std::optional<QString> publishedFileId() const;
  bool cSharpMod() const;
  bool xmlPatchMod() const;
  std::optional<QString> previewImgPath() const;

 private:
  static QString newUuid();

  QString m_uuid = newUuid();
  std::optional<QString> m_modPath;
  mutable std::optional<std::optional<QString>> m_publishedFileIdCache;
  mutable std::optional<bool> m_cSharpModCache;
  mutable std::optional<bool> m_xmlPatchModCache;
};

class ScenarioMod : public ListedMod {
 public:
  QString summary;
};

// A mod with an About.xml: authors, version, rules, and DLC metadata.
class AboutXmlMod : public ListedMod {
 public:
  CaseInsensitiveStr packageId{QStringLiteral("invalid.mod")};
  QStringList authors;
  QString modVersion;
  std::optional<QString> modIconPath;
  qint64 steamAppId = -1;
  QString url;
  double mtime = 0.0;
  BaseRules aboutRules;

  // Display name of the RimWorld DLC this package id belongs to, if any.
  std::optional<QString> getDlcName() const;
};

using ListedModPtr = std::shared_ptr<ListedMod>;

// Pre-computed dependency graphs and tier classification for sorting.
struct CompiledDependencyData {
  QHash<QString, QSet<QString>> depsGraph;
  QHash<QString, QSet<QString>> revDepsGraph;
  QSet<QString> tierZeroMods;
  QSet<QString> tierOneMods;
  QSet<QString> tierThreeMods;
  QHash<QString, QSet<QString>> incompatibilities;
  QHash<QString, QSet<QString>> declaredIncompatibilities;
};

// Glaze schemas for community rules and the Steam Workshop database. Fields the
// upstream JSON may write either as a single value or as a list use
// std::variant.

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct SubExternalRule {
  std::variant<std::vector<std::string>, std::string> name = std::string{};
  std::variant<std::vector<std::string>, std::string> comment = std::string{};
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct SubExternalBoolRule {
  bool value = false;
  std::variant<std::vector<std::string>, std::string> comment = std::string{};
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct ExternalRule {
  std::map<std::string, SubExternalRule> loadAfter;
  std::map<std::string, SubExternalRule> loadBefore;
  SubExternalBoolRule loadTop;
  SubExternalBoolRule loadBottom;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct ExternalRulesSchema {
  std::int64_t timestamp = 0;
  std::map<std::string, ExternalRule> rules;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct SteamDbEntryDependency {
  std::string name;
  std::string url;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct SteamDbEntryBlacklist {
  bool value = false;
  std::string comment;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct SteamDbEntry {
  bool unpublished = false;
  std::string url;
  std::string packageId;
  std::variant<std::vector<std::optional<std::string>>, std::string,
               std::nullptr_t>
      gameVersions = std::vector<std::optional<std::string>>{};
  std::string steamName;
  std::string name;
  std::variant<std::vector<std::string>, std::string, std::nullptr_t> authors =
      std::string{};
  std::map<std::string,
           std::variant<std::vector<std::string>, SteamDbEntryDependency>>
      dependencies;
  SteamDbEntryBlacklist blacklist;
  std::vector<std::map<std::string, std::string>> tags;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct SteamDbSchema {
  std::int64_t version = 0;
  std::map<std::string, SteamDbEntry> database;
};
// NOLINTEND(readability-identifier-naming)

enum class WorkshopUpdateStatus {
  kSuccess,
  kNoWorkshopMods,
  kPartial,
  kFailed,
};

struct WorkshopUpdateResult {
  WorkshopUpdateStatus status = WorkshopUpdateStatus::kFailed;
  int modsChecked = 0;
  int modsUpdated = 0;
  QStringList failedPfids;
  QStringList errors;
};

}  // namespace px::modrim

// Package ids are JSON strings on the wire.
template <>
struct glz::to<glz::JSON, px::modrim::CaseInsensitiveStr> {
  template <auto Opts>
  static void op(const px::modrim::CaseInsensitiveStr& value, auto&& ctx,
                 auto&& b, auto&& ix) {
    glz::to<glz::JSON, std::string>::template op<Opts>(
        value.str().toStdString(), ctx, b, ix);
  }
};

template <>
struct glz::from<glz::JSON, px::modrim::CaseInsensitiveStr> {
  template <auto Opts>
  static void op(px::modrim::CaseInsensitiveStr& value, auto&& ctx, auto&& it,
                 auto&& end) {
    std::string text;
    glz::from<glz::JSON, std::string>::template op<Opts>(text, ctx, it, end);
    value = px::modrim::CaseInsensitiveStr(QString::fromStdString(text));
  }
};
