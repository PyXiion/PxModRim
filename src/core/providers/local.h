#pragma once

#include "core/providers/base.h"

namespace px::modrim::providers {

// Mods the user keeps in the local mods folder: directories without a published file id.
class LocalModProvider final : public BaseModProvider {
public:
    LocalModProvider(QString localPath, QThreadPool* pool = nullptr,
                     MetadataCacheStore* metadataCache = nullptr);

    QCoro::Task<Result<ModMap>> discover(QString target_version, Profiler* timer = nullptr,
                                         MetadataCacheStore* metadata_cache = nullptr,
                                         bool force_reparse = false) override;
};

// Mods downloaded by the app into the local folder: directories that carry a published file id.
class DownloadedModProvider : public BaseModProvider {
public:
    DownloadedModProvider(QString localPath, QThreadPool* pool = nullptr,
                          MetadataCacheStore* metadataCache = nullptr);

    QCoro::Task<Result<ModMap>> discover(QString target_version, Profiler* timer = nullptr,
                                         MetadataCacheStore* metadata_cache = nullptr,
                                         bool force_reparse = false) override;

protected:
    DownloadedModProvider(QString providerId, QString path, QThreadPool* pool,
                          MetadataCacheStore* metadataCache);
};

// Steam Workshop subscriptions: same layout as downloaded mods, under the workshop root.
class SteamWorkshopModProvider final : public DownloadedModProvider {
public:
    SteamWorkshopModProvider(QString workshopPath, QThreadPool* pool = nullptr,
                             MetadataCacheStore* metadataCache = nullptr);
};

}  // namespace px::modrim::providers
