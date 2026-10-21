#include "core/providers/core.hpp"

#include <QDir>
#include <QFileInfo>

namespace px::modrim::providers {

CoreModProvider::CoreModProvider(QString gamePath,
                                 MetadataCacheStore* metadataCache)
    : BaseModProvider(QStringLiteral("core"), QStringLiteral("#e67e22"),
                      std::move(gamePath), nullptr, metadataCache) {}

QList<ModDirectory> CoreModProvider::scanDirectories() const {
  const QString dataDir = QDir(path()).filePath(QStringLiteral("Data"));
  if (!QFileInfo::exists(dataDir)) {
    return {};
  }
  return scanModDirectory(dataDir);
}

}  // namespace px::modrim::providers
