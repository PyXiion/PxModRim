#include "core/utils.hpp"

#include <QDir>
#include <QFileInfo>

namespace px::modrim {

std::optional<QString> findAboutXml(const QString& modPath) {
  if (!QFileInfo(modPath).isDir()) {
    return std::nullopt;
  }
  const QDir modDir(modPath);
  QString candidate = modDir.filePath(QStringLiteral("About/About.xml"));
  if (QFileInfo(candidate).isFile()) {
    return candidate;
  }

  // Only the first directory whose name matches "about" (any case) is searched,
  // as in the legacy implementation.
  const QFileInfoList entries = modDir.entryInfoList(
      QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
  for (const QFileInfo& entry : entries) {
    if (entry.fileName().toLower() != QStringLiteral("about") ||
        !entry.isDir()) {
      continue;
    }
    const QDir aboutDir(entry.absoluteFilePath());
    const QFileInfoList children = aboutDir.entryInfoList(
        QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo& child : children) {
      if (child.fileName().toLower() == QStringLiteral("about.xml")) {
        return child.absoluteFilePath();
      }
    }
    break;
  }
  return std::nullopt;
}

bool isAsciiDigits(QStringView text) {
  if (text.isEmpty()) {
    return false;
  }
  for (const QChar ch : text) {
    if (ch.unicode() < u'0' || ch.unicode() > u'9') {
      return false;
    }
  }
  return true;
}

}  // namespace px::modrim
