#include "core/models/metadata/structures.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QUuid>
#include <utility>

#include "core/utils.hpp"

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
    QString candidate = text.trimmed();
    if (isAsciiDigits(candidate)) {
      return candidate;
    }
  } else if (declaration.error() == QFileDevice::PermissionsError ||
             QFileInfo::exists(declaration.fileName())) {
    return std::nullopt;
  }

  const QString folder = pathLeaf(directory);
  return isAsciiDigits(folder) ? std::optional<QString>(folder) : std::nullopt;
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

std::optional<QString> ListedMod::publishedFileId() const {
  if (!m_workshopIdReady) {
    m_workshopId =
        m_directory ? readWorkshopNumber(*m_directory) : std::nullopt;
    m_workshopIdReady = true;
  }
  return m_workshopId;
}

}  // namespace px::modrim
