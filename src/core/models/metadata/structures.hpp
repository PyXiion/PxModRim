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

  bool contains(QStringView value) const;
  bool contains(const CaseInsensitiveStr& value) const;
  void add(QStringView value);
  void add(const CaseInsensitiveStr& value);

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

  QString uuid() const;
  const std::optional<QString>& modPath() const noexcept { return m_directory; }
  Result<void> setModPath(const QString& path);
  std::optional<QString> publishedFileId() const;

 private:
  QString m_identity = makeIdentity();
  std::optional<QString> m_directory;
  mutable bool m_workshopIdReady = false;
  mutable std::optional<QString> m_workshopId;

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
};

using ListedModPtr = std::shared_ptr<ListedMod>;

struct ModsConfig {
  QString version;
  QList<CaseInsensitiveStr> activeMods;
  QList<CaseInsensitiveStr> knownExpansions;
};

struct ReplacementInfo {
  QString name;
  QString author;
  QString packageid;
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
