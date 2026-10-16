#include "core/providers/local.hpp"

#include <QDir>
#include <QFileInfo>

#include <spdlog/spdlog.h>

namespace px::modrim::providers {

namespace {

// Runs off the UI thread: one directory listing plus a published-file-id probe
// per mod.
QList<ModDirectory> scanWithPublishedIds(const QString& root,
                                         bool hasPublishedFileId) {
  QList<ModDirectory> dirs;
  for (const ModDirectory& dir : scanModDirectory(root)) {
    const bool hasId = QFileInfo::exists(
        QDir(dir.modPath)
            .filePath(QStringLiteral("About/PublishedFileId.txt")));
    if (hasId == hasPublishedFileId) {
      dirs.append(dir);
    }
  }
  return dirs;
}

}  // namespace

LocalModProvider::LocalModProvider(QString localPath, QThreadPool* pool,
                                   MetadataCacheStore* metadataCache)
    : BaseModProvider(QStringLiteral("local"), QStringLiteral("#2ecc71"),
                      std::move(localPath), pool, metadataCache) {}

QCoro::Task<Result<ModMap>> LocalModProvider::discover(
    QString targetVersion, Profiler* timer, MetadataCacheStore* metadataCache,
    bool forceReparse) {
  Profiler ownTimer;
  Profiler& tm = timer ? *timer : ownTimer;

  if (!QFileInfo::exists(path())) {
    spdlog::debug("LocalModProvider path does not exist: {}",
                  path().toStdString());
    co_return ModMap{};
  }
  spdlog::debug("LocalModProvider scanning: {}", path().toStdString());

  QList<ModDirectory> dirs;
  {
    auto scope = tm("scan_dir");
    dirs = co_await runOnPool<QList<ModDirectory>>(
        nullptr, [root = path()] { return scanWithPublishedIds(root, false); });
  }

  auto discovered = co_await loadMods(std::move(dirs), std::move(targetVersion),
                                      tm, metadataCache, forceReparse);
  if (discovered) {
    spdlog::info("LocalModProvider discovered {} mods", discovered->size());
  }
  co_return discovered;
}

DownloadedModProvider::DownloadedModProvider(QString localPath,
                                             QThreadPool* pool,
                                             MetadataCacheStore* metadataCache)
    : DownloadedModProvider(QStringLiteral("downloaded"), std::move(localPath),
                            pool, metadataCache) {}

DownloadedModProvider::DownloadedModProvider(QString providerId, QString path,
                                             QThreadPool* pool,
                                             MetadataCacheStore* metadataCache)
    : BaseModProvider(std::move(providerId), QStringLiteral("#3498db"),
                      std::move(path), pool, metadataCache) {}

QCoro::Task<Result<ModMap>> DownloadedModProvider::discover(
    QString targetVersion, Profiler* timer, MetadataCacheStore* metadataCache,
    bool forceReparse) {
  Profiler ownTimer;
  Profiler& tm = timer ? *timer : ownTimer;

  if (!QFileInfo::exists(path())) {
    spdlog::debug("DownloadedModProvider path does not exist: {}",
                  path().toStdString());
    co_return ModMap{};
  }
  spdlog::debug("DownloadedModProvider scanning: {}", path().toStdString());

  QList<ModDirectory> dirs;
  {
    auto scope = tm("scan_dir");
    dirs = co_await runOnPool<QList<ModDirectory>>(
        nullptr, [root = path()] { return scanWithPublishedIds(root, true); });
  }

  auto discovered = co_await loadMods(std::move(dirs), std::move(targetVersion),
                                      tm, metadataCache, forceReparse);
  if (discovered) {
    spdlog::info("{} discovered {} mods", providerId().toStdString(),
                 discovered->size());
  }
  co_return discovered;
}

SteamWorkshopModProvider::SteamWorkshopModProvider(
    QString workshopPath, QThreadPool* pool, MetadataCacheStore* metadataCache)
    : DownloadedModProvider(QStringLiteral("steam"), std::move(workshopPath),
                            pool, metadataCache) {}

}  // namespace px::modrim::providers
