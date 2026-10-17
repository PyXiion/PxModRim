#include "core/models/metadata/structures.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QUuid>
#include <algorithm>
#include <utility>

#include "core/constants.hpp"

namespace px::modrim {
namespace {

QString pathLeaf(QStringView path) {
  while (!path.isEmpty()) {
    if (path.endsWith(QLatin1Char('/'))) {
      path.chop(1);
    } else if (path.endsWith(QLatin1StringView("/."))) {
      path.chop(2);
    } else {
      break;
    }
  }
  if (path == QLatin1StringView(".")) {
    return {};
  }
  return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1).toString();
}

bool isWorkshopNumber(QStringView text) {
  return !text.isEmpty() && std::ranges::all_of(text, [](QChar character) {
    return character >= QLatin1Char('0') && character <= QLatin1Char('9');
  });
}

std::optional<QString> readWorkshopNumber(const QString& directory) {
  QFile declaration(
      QDir(directory).filePath(QStringLiteral("About/PublishedFileId.txt")));
  if (declaration.open(QIODevice::ReadOnly)) {
    const QByteArray bytes = declaration.readAll();
    if (declaration.error() != QFileDevice::NoError) {
      return std::nullopt;
    }
    QStringDecoder decoder(QStringDecoder::Utf8,
                           QStringConverter::Flag::Stateless);
    const QString text = decoder(bytes);
    if (decoder.hasError()) {
      return std::nullopt;
    }
    const QString candidate = text.trimmed();
    if (isWorkshopNumber(candidate)) {
      return candidate;
    }
  } else if (declaration.error() == QFileDevice::PermissionsError ||
             QFileInfo::exists(declaration.fileName())) {
    return std::nullopt;
  }

  const QString folder = pathLeaf(directory);
  return isWorkshopNumber(folder) ? std::optional<QString>(folder)
                                  : std::nullopt;
}

}  // namespace

CaseInsensitiveStr::CaseInsensitiveStr(QStringView pid)
    : m_text(pid.toString().toLower()) {}

CaseInsensitiveSet::CaseInsensitiveSet(const QStringList& values) {
  m_members.reserve(values.size());
  for (const QString& value : values) {
    add(value);
  }
}

CaseInsensitiveSet::CaseInsensitiveSet(const QList<CaseInsensitiveStr>& values)
    : m_members(values.cbegin(), values.cend()) {}

bool CaseInsensitiveSet::contains(QStringView value) const {
  return contains(CaseInsensitiveStr(value));
}

bool CaseInsensitiveSet::contains(const CaseInsensitiveStr& value) const {
  return m_members.contains(value);
}

void CaseInsensitiveSet::add(QStringView value) {
  add(CaseInsensitiveStr(value));
}

void CaseInsensitiveSet::add(const CaseInsensitiveStr& value) {
  m_members.insert(value);
}

void CaseInsensitiveSet::discard(QStringView value) {
  discard(CaseInsensitiveStr(value));
}

void CaseInsensitiveSet::discard(const CaseInsensitiveStr& value) {
  m_members.remove(value);
}

CaseInsensitiveSet CaseInsensitiveSet::operator|(
    const CaseInsensitiveSet& other) const {
  CaseInsensitiveSet combined = *this;
  combined.m_members.unite(other.m_members);
  return combined;
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

QString ListedMod::makeIdentity() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ListedMod::uuid() const { return m_directory.value_or(m_identity); }

Result<void> ListedMod::setModPath(const QString& path) {
  if (m_directory) {
    return std::unexpected(
        Error{QStringLiteral("Mod path already set. Cannot override.")});
  }
  m_directory = path;
  m_workshopIdReady = false;
  m_workshopId.reset();
  return {};
}

std::optional<QString> ListedMod::modFolder() const {
  if (!m_directory) {
    return std::nullopt;
  }
  QString folder = pathLeaf(*m_directory);
  const qsizetype extension = folder.lastIndexOf(QLatin1Char('.'));
  if (extension > 0 && folder != QLatin1StringView("..")) {
    folder.truncate(extension);
  }
  return folder;
}

qint64 ListedMod::internalTimeTouched() const {
  if (!m_directory) {
    return -1;
  }
  const QDateTime modified = QFileInfo(*m_directory).lastModified();
  return modified.isValid() ? modified.toSecsSinceEpoch() : -1;
}

std::optional<QString> ListedMod::publishedFileId() const {
  if (!m_workshopIdReady) {
    m_workshopId =
        m_directory ? readWorkshopNumber(*m_directory) : std::nullopt;
    m_workshopIdReady = true;
  }
  return m_workshopId;
}

bool ListedMod::cSharpMod() const {
  if (!m_hasAssemblies) {
    bool found = false;
    if (m_directory) {
      QDirIterator entries(
          QDir(*m_directory).filePath(QStringLiteral("Assemblies")),
          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
              QDir::System);
      found = entries.hasNext();
    }
    m_hasAssemblies = found;
  }
  return *m_hasAssemblies;
}

bool ListedMod::xmlPatchMod() const {
  if (!m_hasPatches) {
    bool found = false;
    if (m_directory) {
      QDirIterator entries(
          QDir(*m_directory).filePath(QStringLiteral("Patches")),
          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
              QDir::System);
      while (entries.hasNext()) {
        entries.next();
        const QString name = entries.fileName();
        if (name.size() > 4 && name.endsWith(QLatin1StringView(".xml"))) {
          found = true;
          break;
        }
      }
    }
    m_hasPatches = found;
  }
  return *m_hasPatches;
}

std::optional<QString> ListedMod::previewImgPath() const {
  if (m_directory) {
    const QString preview =
        QDir(*m_directory).filePath(QStringLiteral("About/Preview.png"));
    if (QFileInfo::exists(preview)) {
      return preview;
    }
  }
  return std::nullopt;
}

std::optional<QString> AboutXmlMod::getDlcName() const {
  if (const RimWorldDlc* dlc = findRimWorldDlcByPackageId(packageId.str())) {
    return QString(dlc->name);
  }
  return std::nullopt;
}

bool ModsConfig::checkActiveDuplicates() const {
  QSet<CaseInsensitiveStr> visited;
  visited.reserve(activeMods.size());
  for (const CaseInsensitiveStr& packageId : activeMods) {
    if (visited.contains(packageId)) {
      return true;
    }
    visited.insert(packageId);
  }
  return false;
}

bool ModsConfig::checkExpansionsDuplicates() const {
  QSet<CaseInsensitiveStr> visited;
  visited.reserve(knownExpansions.size());
  for (const CaseInsensitiveStr& packageId : knownExpansions) {
    if (visited.contains(packageId)) {
      return true;
    }
    visited.insert(packageId);
  }
  return false;
}

}  // namespace px::modrim
