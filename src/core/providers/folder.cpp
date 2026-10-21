#include "core/providers/folder.hpp"

#include <QDir>
#include <QFileInfo>

#include <spdlog/spdlog.h>

namespace px::modrim::providers {

FolderModProvider::FolderModProvider(QString providerId, QString color,
                                     QString path, bool hasPublishedFileId,
                                     QThreadPool* pool,
                                     MetadataCacheStore* metadataCache)
    : BaseModProvider(std::move(providerId), std::move(color), std::move(path),
                      pool, metadataCache),
      m_hasPublishedFileId(hasPublishedFileId) {}

QList<ModDirectory> FolderModProvider::scanDirectories() const {
  if (!QFileInfo::exists(path())) {
    spdlog::debug("{} path does not exist: {}", providerId().toStdString(),
                  path().toStdString());
    return {};
  }
  QList<ModDirectory> dirs;
  for (const ModDirectory& dir : scanModDirectory(path())) {
    const bool hasId = QFileInfo::exists(
        QDir(dir.modPath)
            .filePath(QStringLiteral("About/PublishedFileId.txt")));
    if (hasId == m_hasPublishedFileId) {
      dirs.append(dir);
    }
  }
  return dirs;
}

}  // namespace px::modrim::providers
