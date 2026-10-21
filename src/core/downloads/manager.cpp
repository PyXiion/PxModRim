#include "core/downloads/manager.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace px::modrim {

DownloadManager::DownloadManager() : Plugin(kDownloadsManagerName) {}

void DownloadManager::registerDownloader(Downloader& downloader) {
  m_downloaders.push_back(&downloader);
}

void DownloadManager::notifyBusy(bool busy) {
  if (busy) {
    m_cancelled = false;
  }
  busyChanged.notify(isDownloading());
}

bool DownloadManager::available() const noexcept {
  return !m_downloaders.empty();
}

bool DownloadManager::isDownloading() const {
  return std::ranges::any_of(
      m_downloaders, [](const Downloader* d) { return d->isDownloading(); });
}

QSet<QString> DownloadManager::activeIds() const {
  QSet<QString> ids;
  for (const auto* downloader : m_downloaders) {
    ids |= downloader->activeIds();
  }
  return ids;
}

Downloader* DownloadManager::sourceOf(const ListedMod& mod) const {
  for (auto* downloader : m_downloaders) {
    if (downloader->updatableId(mod)) {
      return downloader;
    }
  }
  return nullptr;
}

std::optional<QString> DownloadManager::updatableId(
    const ListedMod& mod) const {
  for (const auto* downloader : m_downloaders) {
    if (auto id = downloader->updatableId(mod)) {
      return id;
    }
  }
  return std::nullopt;
}

QStringList DownloadManager::updatableIds(
    const QList<ListedModPtr>& mods) const {
  QStringList ids;
  QSet<QString> seen;
  for (const auto& mod : mods) {
    auto id = updatableId(*mod);
    if (id && !seen.contains(*id)) {
      seen.insert(*id);
      ids.append(std::move(*id));
    }
  }
  return ids;
}

QHash<QString, QString> DownloadManager::namesById(
    const QList<ListedModPtr>& mods) const {
  QHash<QString, QString> names;
  for (const auto& mod : mods) {
    if (auto id = updatableId(*mod)) {
      names.insert(*id, mod->name);
    }
  }
  return names;
}

std::optional<double> DownloadManager::lastSynced(const ListedMod& mod) const {
  const Downloader* source = sourceOf(mod);
  return source ? source->lastSynced(mod) : std::nullopt;
}

QCoro::Task<Result<DownloadResult>> DownloadManager::downloadMods(
    QStringList modIds) {
  // Ids are grouped by owning downloader in first-seen order; duplicates are
  // dropped.
  std::vector<std::pair<Downloader*, QStringList>> groups;
  QSet<QString> seen;
  for (const auto& modId : modIds) {
    if (seen.contains(modId)) {
      continue;
    }
    seen.insert(modId);

    Downloader* owner = nullptr;
    for (auto* downloader : m_downloaders) {
      if (downloader->accepts(modId)) {
        owner = downloader;
        break;
      }
    }
    if (!owner) {
      co_return std::unexpected(
          Error{QStringLiteral("No download source handles '%1'.").arg(modId)});
    }
    auto group = std::ranges::find(groups, owner,
                                   &std::pair<Downloader*, QStringList>::first);
    if (group == groups.end()) {
      groups.emplace_back(owner, QStringList{});
      group = std::prev(groups.end());
    }
    group->second.append(modId);
  }
  if (groups.empty()) {
    co_return std::unexpected(
        Error{QStringLiteral("No mods selected for download.")});
  }
  if (isDownloading()) {
    co_return std::unexpected(
        Error{QStringLiteral("A download is already running.")});
  }

  m_cancelled = false;
  DownloadResult total;
  for (auto& [downloader, group] : groups) {
    if (m_cancelled) {
      break;
    }
    auto result = co_await downloader->downloadMods(group);
    if (!result) {
      co_return std::unexpected(result.error());
    }
    total.succeeded += result->succeeded;
    total.failed += result->failed;
    total.changed += result->changed;
  }
  co_return total;
}

bool DownloadManager::cancelled() const noexcept { return m_cancelled; }

void DownloadManager::cancel() {
  m_cancelled = true;
  for (auto* downloader : m_downloaders) {
    if (downloader->isDownloading()) {
      downloader->cancel();
    }
  }
}

}  // namespace px::modrim
