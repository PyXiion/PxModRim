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

class CaseInsensitiveStr {
 public:
  CaseInsensitiveStr() = default;
  explicit CaseInsensitiveStr(QStringView pid);

  const QString& str() const noexcept { return m_text; }

  friend bool operator==(const CaseInsensitiveStr&,
                         const CaseInsensitiveStr&) = default;
  friend size_t qHash(const CaseInsensitiveStr& key, size_t seed = 0) noexcept {
    return qHash(key.m_text, seed);
  }

 private:
  QString m_text;
};

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
  CaseInsensitiveSet operator|(const CaseInsensitiveSet& other) const;

  qsizetype size() const noexcept { return m_members.size(); }
  bool empty() const noexcept { return m_members.isEmpty(); }
  auto begin() const { return m_members.cbegin(); }
  auto end() const { return m_members.cend(); }

  friend bool operator==(const CaseInsensitiveSet&,
                         const CaseInsensitiveSet&) = default;

 private:
  QSet<CaseInsensitiveStr> m_members;
};

struct DependencyMod {
  QString name = QStringLiteral("Unknown Mod Name");
  CaseInsensitiveStr packageId{QStringLiteral("invalid.mod")};
  QString workshopUrl;
  CaseInsensitiveSet alternativePackageIds;
};

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
  void setDependency(DependencyMod dependency);
};

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
      "This mod is considered invalid by PxModRim and by the game itself. "
      "It has no About/About.xml file, so it is most likely a leftover "
      "from earlier usage (for example a removed mod whose folder stayed "
      "behind).");
  QString providerId;
  bool obsolete = false;
  bool dbBuilderNoName = false;

  virtual QString uuid() const;
  const std::optional<QString>& modPath() const noexcept { return m_directory; }
  Result<void> setModPath(const QString& path);
  std::optional<QString> modFolder() const;
  qint64 internalTimeTouched() const;
  std::optional<QString> publishedFileId() const;
  bool cSharpMod() const;
  bool xmlPatchMod() const;
  std::optional<QString> previewImgPath() const;

 private:
  QString m_identity = makeIdentity();
  std::optional<QString> m_directory;
  mutable bool m_workshopIdReady = false;
  mutable std::optional<QString> m_workshopId;
  mutable std::optional<bool> m_hasAssemblies;
  mutable std::optional<bool> m_hasPatches;

  static QString makeIdentity();
};

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

  std::optional<QString> getDlcName() const;
};

class ScenarioMod : public ListedMod {
 public:
  QString summary;
};

using ListedModPtr = std::shared_ptr<ListedMod>;

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

struct CompiledDependencyData {
  QHash<QString, QSet<QString>> depsGraph;
  QHash<QString, QSet<QString>> revDepsGraph;
  QSet<QString> tierZeroMods;
  QSet<QString> tierOneMods;
  QSet<QString> tierThreeMods;
  QHash<QString, QSet<QString>> incompatibilities;
  QHash<QString, QSet<QString>> declaredIncompatibilities;
};

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

template <>
struct glz::to<glz::JSON, px::modrim::CaseInsensitiveStr> {
  template <auto Opts>
  static void op(const px::modrim::CaseInsensitiveStr& value, auto&& ctx,
                 auto&& b, auto&& ix) {
    const std::string text = value.str().toStdString();
    glz::to<glz::JSON, std::string>::template op<Opts>(text, ctx, b, ix);
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
