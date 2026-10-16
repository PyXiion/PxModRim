#include "core/models/metadata/structures.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include "core/constants.hpp"
#include "core/utils.hpp"

namespace px::modrim {

namespace {

// Python's Path.stem / Path.suffix rule: a leading dot does not start an
// extension.
qsizetype lastExtensionDot(const QString& name) {
  const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
  return (dot > 0 && dot < name.size() - 1) ? dot : -1;
}

QString pathStem(const QString& name) {
  const qsizetype dot = lastExtensionDot(name);
  return dot < 0 ? name : name.left(dot);
}

QString pathSuffix(const QString& name) {
  const qsizetype dot = lastExtensionDot(name);
  return dot < 0 ? QString() : name.mid(dot);
}

bool hasEntries(const QString& dirPath) {
  return !QDir(dirPath)
              .entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                         QDir::NoDotAndDotDot)
              .isEmpty();
}

}  // namespace

CaseInsensitiveSet::CaseInsensitiveSet(const QStringList& values) {
  for (const QString& value : values) {
    m_data.insert(CaseInsensitiveStr(value));
  }
}

CaseInsensitiveSet::CaseInsensitiveSet(
    const QList<CaseInsensitiveStr>& values) {
  for (const CaseInsensitiveStr& value : values) {
    m_data.insert(value);
  }
}

bool CaseInsensitiveSet::contains(QStringView value) const {
  return m_data.contains(CaseInsensitiveStr(value));
}

bool CaseInsensitiveSet::contains(const CaseInsensitiveStr& value) const {
  return m_data.contains(value);
}

void CaseInsensitiveSet::add(QStringView value) {
  m_data.insert(CaseInsensitiveStr(value));
}

void CaseInsensitiveSet::add(const CaseInsensitiveStr& value) {
  m_data.insert(value);
}

void CaseInsensitiveSet::discard(QStringView value) {
  m_data.remove(CaseInsensitiveStr(value));
}

void CaseInsensitiveSet::discard(const CaseInsensitiveStr& value) {
  m_data.remove(value);
}

CaseInsensitiveSet CaseInsensitiveSet::operator|(
    const CaseInsensitiveSet& other) const {
  CaseInsensitiveSet result = *this;
  result.m_data.unite(other.m_data);
  return result;
}

bool ModsConfig::checkActiveDuplicates() const {
  const QSet<CaseInsensitiveStr> unique(activeMods.cbegin(), activeMods.cend());
  return activeMods.size() != unique.size();
}

bool ModsConfig::checkExpansionsDuplicates() const {
  const QSet<CaseInsensitiveStr> unique(knownExpansions.cbegin(),
                                        knownExpansions.cend());
  return knownExpansions.size() != unique.size();
}

const DependencyMod* BaseRules::findDependency(
    const CaseInsensitiveStr& packageId) const {
  for (const DependencyMod& dependency : dependencies) {
    if (dependency.packageId == packageId) {
      return &dependency;
    }
  }
  return nullptr;
}

void BaseRules::setDependency(DependencyMod dependency) {
  for (DependencyMod& existing : dependencies) {
    if (existing.packageId == dependency.packageId) {
      existing = std::move(dependency);
      return;
    }
  }
  dependencies.append(std::move(dependency));
}

QString ListedMod::newUuid() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ListedMod::uuid() const {
  if (m_modPath) {
    return *m_modPath;
  }
  return m_uuid;
}

Result<void> ListedMod::setModPath(const QString& path) {
  if (m_modPath) {
    return std::unexpected(
        Error{QStringLiteral("Mod path already set. Cannot override.")});
  }
  m_modPath = path;
  m_publishedFileIdCache.reset();
  return {};
}

std::optional<QString> ListedMod::modFolder() const {
  if (!m_modPath) {
    return std::nullopt;
  }
  return pathStem(QFileInfo(QDir::cleanPath(*m_modPath)).fileName());
}

qint64 ListedMod::internalTimeTouched() const {
  if (!m_modPath || !QFileInfo::exists(*m_modPath)) {
    return -1;
  }
  return QFileInfo(*m_modPath).lastModified().toSecsSinceEpoch();
}

std::optional<QString> ListedMod::publishedFileId() const {
  if (!m_publishedFileIdCache) {
    m_publishedFileIdCache = [this]() -> std::optional<QString> {
      if (!m_modPath) {
        return std::nullopt;
      }
      const QString pfidPath =
          QDir(*m_modPath)
              .filePath(QStringLiteral("About/PublishedFileId.txt"));
      if (QFileInfo::exists(pfidPath)) {
        QFile file(pfidPath);
        if (!file.open(QIODevice::ReadOnly)) {
          return std::nullopt;
        }
        // utf-8-sig: drop a leading byte-order mark before trimming.
        QString content = QString::fromUtf8(file.readAll());
        if (content.startsWith(QChar(0xFEFF))) {
          content.remove(0, 1);
        }
        content = content.trimmed();
        if (isAsciiDigits(content)) {
          return content;
        }
      }
      const std::optional<QString> folder = modFolder();
      if (folder && isAsciiDigits(*folder)) {
        return folder;
      }
      return std::nullopt;
    }();
  }
  return *m_publishedFileIdCache;
}

bool ListedMod::cSharpMod() const {
  if (!m_cSharpModCache) {
    m_cSharpModCache = [this] {
      if (!m_modPath) {
        return false;
      }
      const QString assemblies =
          QDir(*m_modPath).filePath(QStringLiteral("Assemblies"));
      return QFileInfo(assemblies).isDir() && hasEntries(assemblies);
    }();
  }
  return *m_cSharpModCache;
}

bool ListedMod::xmlPatchMod() const {
  if (!m_xmlPatchModCache) {
    m_xmlPatchModCache = [this] {
      if (!m_modPath) {
        return false;
      }
      const QString patches =
          QDir(*m_modPath).filePath(QStringLiteral("Patches"));
      if (!QFileInfo(patches).isDir()) {
        return false;
      }
      const QFileInfoList entries =
          QDir(patches).entryInfoList(QDir::AllEntries | QDir::Hidden |
                                      QDir::System | QDir::NoDotAndDotDot);
      for (const QFileInfo& entry : entries) {
        if (pathSuffix(entry.fileName()) == QStringLiteral(".xml")) {
          return true;
        }
      }
      return false;
    }();
  }
  return *m_xmlPatchModCache;
}

std::optional<QString> ListedMod::previewImgPath() const {
  if (!m_modPath) {
    return std::nullopt;
  }
  const QString candidate =
      QDir(*m_modPath).filePath(QStringLiteral("About/Preview.png"));
  if (QFileInfo::exists(candidate)) {
    return candidate;
  }
  return std::nullopt;
}

std::optional<QString> AboutXmlMod::getDlcName() const {
  if (const RimWorldDlc* dlc = findRimWorldDlcByPackageId(packageId.str())) {
    return QString(dlc->name);
  }
  return std::nullopt;
}

}  // namespace px::modrim
