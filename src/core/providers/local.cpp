#include "core/providers/local.hpp"

#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>

namespace px::modrim::providers {

namespace {

struct ScannedDirectory {
    ModDirectory dir;
    bool hasPublishedFileId = false;
};

// Runs off the UI thread: one directory listing plus a published-file-id probe per mod.
QList<ScannedDirectory> scanWithPublishedIds(const QString& root) {
    QList<ScannedDirectory> scanned;
    for (const ModDirectory& dir : scanModDirectory(root)) {
        const bool hasId = QFileInfo::exists(
            QDir(dir.modPath).filePath(QStringLiteral("About/PublishedFileId.txt")));
        scanned.append(ScannedDirectory{dir, hasId});
    }
    return scanned;
}

}  // namespace

LocalModProvider::LocalModProvider(QString localPath, QThreadPool* pool,
                                   MetadataCacheStore* metadataCache)
    : BaseModProvider(QStringLiteral("local"), QStringLiteral("#2ecc71"), std::move(localPath),
                      pool, metadataCache) {}

QCoro::Task<Result<ModMap>> LocalModProvider::discover(QString target_version, Profiler* timer,
                                                       MetadataCacheStore* metadata_cache,
                                                       bool force_reparse) {
    Profiler ownTimer;
    Profiler& tm = timer ? *timer : ownTimer;

    if (!QFileInfo::exists(path())) {
        spdlog::debug("LocalModProvider path does not exist: {}", path().toStdString());
        co_return ModMap{};
    }
    spdlog::debug("LocalModProvider scanning: {}", path().toStdString());

    QList<ScannedDirectory> scanned;
    {
        auto scope = tm("scan_dir");
        scanned = co_await runOnPool<QList<ScannedDirectory>>(
            nullptr, [root = path()] { return scanWithPublishedIds(root); });
    }
    QList<ModDirectory> dirs;
    for (const ScannedDirectory& item : scanned) {
        if (!item.hasPublishedFileId) {
            dirs.append(item.dir);
        }
    }

    auto discovered = co_await load_mods(std::move(dirs), std::move(target_version), tm,
                                         metadata_cache, force_reparse);
    if (discovered) {
        spdlog::info("LocalModProvider discovered {} mods", discovered->size());
    }
    co_return discovered;
}

DownloadedModProvider::DownloadedModProvider(QString localPath, QThreadPool* pool,
                                             MetadataCacheStore* metadataCache)
    : DownloadedModProvider(QStringLiteral("downloaded"), std::move(localPath), pool,
                            metadataCache) {}

DownloadedModProvider::DownloadedModProvider(QString providerId, QString path,
                                             QThreadPool* pool, MetadataCacheStore* metadataCache)
    : BaseModProvider(std::move(providerId), QStringLiteral("#3498db"), std::move(path), pool,
                      metadataCache) {}

QCoro::Task<Result<ModMap>> DownloadedModProvider::discover(QString target_version,
                                                            Profiler* timer,
                                                            MetadataCacheStore* metadata_cache,
                                                            bool force_reparse) {
    Profiler ownTimer;
    Profiler& tm = timer ? *timer : ownTimer;

    if (!QFileInfo::exists(path())) {
        spdlog::debug("DownloadedModProvider path does not exist: {}", path().toStdString());
        co_return ModMap{};
    }
    spdlog::debug("DownloadedModProvider scanning: {}", path().toStdString());

    QList<ScannedDirectory> scanned;
    {
        auto scope = tm("scan_dir");
        scanned = co_await runOnPool<QList<ScannedDirectory>>(
            nullptr, [root = path()] { return scanWithPublishedIds(root); });
    }
    QList<ModDirectory> dirs;
    for (const ScannedDirectory& item : scanned) {
        if (item.hasPublishedFileId) {
            dirs.append(item.dir);
        }
    }

    auto discovered = co_await load_mods(std::move(dirs), std::move(target_version), tm,
                                         metadata_cache, force_reparse);
    if (discovered) {
        spdlog::info("{} discovered {} mods", provider_id().toStdString(), discovered->size());
    }
    co_return discovered;
}

SteamWorkshopModProvider::SteamWorkshopModProvider(QString workshopPath, QThreadPool* pool,
                                                   MetadataCacheStore* metadataCache)
    : DownloadedModProvider(QStringLiteral("steam"), std::move(workshopPath), pool,
                            metadataCache) {}

}  // namespace px::modrim::providers
