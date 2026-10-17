#include "core/providers/base.hpp"

#include <QDir>
#include <QFileInfo>

#include <spdlog/spdlog.h>

#include "core/models/metadata/parsing.hpp"
#include "core/utils.hpp"

namespace px::modrim::providers {

namespace {

QList<ModCandidate> statCandidates(const QList<ModDirectory>& dirs) {
  QList<ModCandidate> candidates;
  candidates.reserve(dirs.size());
  for (const ModDirectory& dir : dirs) {
    const QFileInfo info(dir.aboutXmlPath);
    if (!info.exists()) {
      continue;
    }
    candidates.append(ModCandidate{
        dir.modPath, dir.aboutXmlPath,
        info.lastModified().toMSecsSinceEpoch() / 1000.0, info.size()});
  }
  return candidates;
}

Result<ParsedMod> parseOne(const ModCandidate& candidate,
                           const QString& targetVersion) {
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

BaseModProvider::BaseModProvider(QString providerId, QString color,
                                 QString path, QThreadPool* pool,
                                 MetadataCacheStore* metadataCache)
    : m_providerId(std::move(providerId)),
      m_color(std::move(color)),
      m_path(std::move(path)),
      m_pool(pool),
      m_metadataCache(metadataCache) {}

QCoro::Task<Result<ModMap>> BaseModProvider::loadMods(
    QList<ModDirectory> dirs, QString targetVersion, Profiler& timer,
    MetadataCacheStore* metadataCache, bool forceReparse) {
  MetadataCacheStore* cache = metadataCache ? metadataCache : m_metadataCache;

  const QList<ModCandidate> candidates =
      co_await runOnPool<QList<ModCandidate>>(
          nullptr, [dirs = std::move(dirs)] { return statCandidates(dirs); });

  ModMap result;
  QList<ModCandidate> missing;
  if (cache && !forceReparse) {
    auto scope = timer("cache_lookup");
    CacheLookup lookup = co_await cache->getMods(candidates, targetVersion);
    for (ListedModPtr& mod : lookup.cached) {
      mod->providerId = m_providerId;
      result.insert(mod->uuid(), mod);
    }
    missing = std::move(lookup.missing);
  } else {
    missing = candidates;
  }

  if (!missing.isEmpty()) {
    auto scope = timer("parse_xml");
    Result<QList<ParsedMod>> parsed =
        co_await parseCandidates(missing, targetVersion);
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
        co_await cache->putMods(std::move(cacheItems), targetVersion);
      }
    }
    for (ParsedMod& item : *parsed) {
      item.mod->providerId = m_providerId;
      result.insert(item.mod->uuid(), item.mod);
    }
  }

  // Warm the disk-backed published-file-id cache off the calling thread.
  const QList<ListedModPtr> mods = result.values();
  co_await runOnPool<void>(nullptr, [mods] {
    for (const ListedModPtr& mod : mods) {
      mod->publishedFileId();
    }
  });
  co_return result;
}

QCoro::Task<Result<QList<ParsedMod>>> BaseModProvider::parseCandidates(
    QList<ModCandidate> missing, QString targetVersion) {
  if (m_pool == nullptr || missing.size() <= 1) {
    co_return co_await runOnPool<Result<QList<ParsedMod>>>(
        nullptr, [missing = std::move(missing),
                  targetVersion = std::move(targetVersion)] {
          QList<ParsedMod> parsed;
          parsed.reserve(missing.size());
          for (const ModCandidate& candidate : missing) {
            Result<ParsedMod> item = parseOne(candidate, targetVersion);
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
    pending.append(
        runOnPool<Result<ParsedMod>>(m_pool, [candidate, targetVersion] {
          return parseOne(candidate, targetVersion);
        }));
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

}  // namespace px::modrim::providers
