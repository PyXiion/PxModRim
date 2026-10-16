#include "core/providers/core.hpp"

#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>

namespace px::modrim::providers {

CoreModProvider::CoreModProvider(QString gamePath, MetadataCacheStore* metadataCache)
    : BaseModProvider(QStringLiteral("core"), QStringLiteral("#e67e22"), std::move(gamePath),
                      nullptr, metadataCache) {}

QCoro::Task<Result<ModMap>> CoreModProvider::discover(QString targetVersion, Profiler* timer,
                                                      MetadataCacheStore* metadataCache,
                                                      bool forceReparse) {
    Profiler ownTimer;
    Profiler& tm = timer ? *timer : ownTimer;

    const QString dataDir = QDir(path()).filePath(QStringLiteral("Data"));
    spdlog::debug("CoreModProvider scanning: {}", dataDir.toStdString());
    if (!QFileInfo::exists(dataDir)) {
        co_return ModMap{};
    }

    QList<ModDirectory> dirs;
    {
        auto scope = tm("scan_dir");
        dirs = co_await runOnPool<QList<ModDirectory>>(
            nullptr, [dataDir] { return scanModDirectory(dataDir); });
    }

    auto discovered = co_await loadMods(std::move(dirs), std::move(targetVersion), tm,
                                         metadataCache, forceReparse);
    if (discovered) {
        spdlog::info("CoreModProvider discovered {} mods", discovered->size());
    }
    co_return discovered;
}

}  // namespace px::modrim::providers
