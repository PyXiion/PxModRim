#include "core/providers/base.h"

#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>

#include "core/models/metadata/parsing.h"
#include "core/utils.h"

namespace px::core::providers {

namespace {

QList<ModCandidate> statCandidates(const QList<ModDirectory>& dirs) {
    QList<ModCandidate> candidates;
    candidates.reserve(dirs.size());
    for (const ModDirectory& dir : dirs) {
        const QFileInfo info(dir.aboutXmlPath);
        if (!info.exists()) {
            continue;
        }
        candidates.append(ModCandidate{dir.modPath, dir.aboutXmlPath,
                                       info.lastModified().toMSecsSinceEpoch() / 1000.0,
                                       info.size()});
    }
    return candidates;
}

Result<ParsedMod> parseOne(const ModCandidate& candidate, const QString& targetVersion) {
    auto mod = createListedModFromPath(candidate.modPath, targetVersion, true,
                                       candidate.aboutXmlPath);
    if (!mod) {
        return std::unexpected(mod.error());
    }
    return ParsedMod{std::move(*mod), candidate};
}

}  // namespace

QList<ModDirectory> scanModDirectory(const QString& modsPath) {
    const QFileInfo root(modsPath);
    if (!root.exists() || !root.isDir()) {
        spdlog::warn("Mod directory not found: {}", modsPath.toStdString());
        return {};
    }

    QList<ModDirectory> found;
    const QDir dir(modsPath);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const QString modPath = dir.filePath(entry.fileName());
        if (const auto aboutXml = findAboutXml(modPath)) {
            found.append(ModDirectory{modPath, *aboutXml});
        }
    }
    return found;
}

BaseModProvider::BaseModProvider(QString provider_id, QString color, QString path,
                                 QThreadPool* pool, MetadataCacheStore* metadata_cache)
    : provider_id_(std::move(provider_id)),
      color_(std::move(color)),
      path_(std::move(path)),
      pool_(pool),
      metadata_cache_(metadata_cache) {}

QCoro::Task<Result<ModMap>> BaseModProvider::load_mods(QList<ModDirectory> dirs,
                                                       QString target_version, Profiler& timer,
                                                       MetadataCacheStore* metadata_cache,
                                                       bool force_reparse) {
    MetadataCacheStore* cache = metadata_cache ? metadata_cache : metadata_cache_;

    const QList<ModCandidate> candidates = co_await runOnPool<QList<ModCandidate>>(
        nullptr, [dirs = std::move(dirs)] { return statCandidates(dirs); });

    ModMap result;
    QList<ModCandidate> missing;
    if (force_reparse) {
        missing = candidates;
    } else if (cache) {
        auto scope = timer("cache_lookup");
        CacheLookup lookup = co_await cache->get_mods(candidates, target_version);
        for (ListedModPtr& mod : lookup.cached) {
            mod->provider_id = provider_id_;
            result.insert(mod->uuid(), mod);
        }
        missing = std::move(lookup.missing);
    } else {
        missing = candidates;
    }

    if (!missing.isEmpty()) {
        auto scope = timer("parse_xml");
        Result<QList<ParsedMod>> parsed = co_await parse_candidates(missing, target_version);
        if (!parsed) {
            co_return std::unexpected(parsed.error());
        }
        if (cache) {
            QList<CachedMod> cacheItems;
            for (const ParsedMod& item : *parsed) {
                if (auto about = std::dynamic_pointer_cast<AboutXmlMod>(item.mod)) {
                    cacheItems.append(CachedMod{std::move(about), item.candidate});
                }
            }
            if (!cacheItems.isEmpty()) {
                co_await cache->put_mods(std::move(cacheItems), target_version);
            }
        }
        for (ParsedMod& item : *parsed) {
            item.mod->provider_id = provider_id_;
            result.insert(item.mod->uuid(), item.mod);
        }
    }

    // Warm the disk-backed published-file-id cache off the calling thread.
    const QList<ListedModPtr> mods = result.values();
    co_await runOnPool<void>(nullptr, [mods] {
        for (const ListedModPtr& mod : mods) {
            mod->published_file_id();
        }
    });
    co_return result;
}

QCoro::Task<Result<QList<ParsedMod>>> BaseModProvider::parse_candidates(
    QList<ModCandidate> missing, QString target_version) {
    if (pool_ == nullptr || missing.size() <= 1) {
        co_return co_await runOnPool<Result<QList<ParsedMod>>>(
            nullptr, [missing = std::move(missing), target_version = std::move(target_version)] {
                QList<ParsedMod> parsed;
                parsed.reserve(missing.size());
                for (const ModCandidate& candidate : missing) {
                    Result<ParsedMod> item = parseOne(candidate, target_version);
                    if (!item) {
                        return Result<QList<ParsedMod>>(std::unexpected(item.error()));
                    }
                    parsed.append(std::move(*item));
                }
                return Result<QList<ParsedMod>>(std::move(parsed));
            });
    }

    QList<QFuture<Result<ParsedMod>>> pending;
    pending.reserve(missing.size());
    for (const ModCandidate& candidate : missing) {
        pending.append(runOnPool<Result<ParsedMod>>(
            pool_, [candidate, target_version] { return parseOne(candidate, target_version); }));
    }
    QList<ParsedMod> parsed;
    parsed.reserve(missing.size());
    for (QFuture<Result<ParsedMod>>& future : pending) {
        Result<ParsedMod> item = co_await future;
        if (!item) {
            co_return std::unexpected(item.error());
        }
        parsed.append(std::move(*item));
    }
    co_return parsed;
}

}  // namespace px::core::providers
