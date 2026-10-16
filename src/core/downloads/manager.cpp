#include "core/downloads/manager.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace px::modrim {

DownloadManager::DownloadManager() : Plugin(kDownloadsManagerName) {}

void DownloadManager::register_downloader(Downloader& downloader) {
    downloaders_.push_back(&downloader);
    downloader.status_message_changed.connect([this](const QString& v) { status_message_changed.notify(v); });
    downloader.download_progress.connect([this](const DownloadProgress& v) { download_progress.notify(v); });
    downloader.download_item_status_changed.connect(
        [this](const DownloadItemStatus& v) { download_item_status_changed.notify(v); });
    downloader.download_item_titled.connect([this](const DownloadItemTitle& v) { download_item_titled.notify(v); });
    downloader.download_phase_changed.connect([this](const QString& v) { download_phase_changed.notify(v); });
    downloader.download_finished.connect([this](const DownloadResult& v) { download_finished.notify(v); });
    downloader.batch_started.connect([this](const QStringList& v) { batch_started.notify(v); });
    downloader.busy_changed.connect([this](bool busy) { on_busy(busy); });
}

void DownloadManager::on_busy(bool busy) {
    if (busy) {
        cancelled_ = false;
    }
    busy_changed.notify(is_downloading());
}

const std::vector<Downloader*>& DownloadManager::downloaders() const noexcept { return downloaders_; }

bool DownloadManager::available() const noexcept { return !downloaders_.empty(); }

bool DownloadManager::is_downloading() const {
    return std::ranges::any_of(downloaders_, [](const Downloader* d) { return d->is_downloading(); });
}

QSet<QString> DownloadManager::active_ids() const {
    QSet<QString> ids;
    for (const auto* downloader : downloaders_) {
        ids |= downloader->active_ids();
    }
    return ids;
}

Downloader* DownloadManager::source_of(const ListedMod& mod) const {
    for (auto* downloader : downloaders_) {
        if (downloader->updatable_id(mod)) {
            return downloader;
        }
    }
    return nullptr;
}

std::optional<QString> DownloadManager::updatable_id(const ListedMod& mod) const {
    const Downloader* source = source_of(mod);
    return source ? source->updatable_id(mod) : std::nullopt;
}

QStringList DownloadManager::updatable_ids(const QList<ListedModPtr>& mods) const {
    QStringList ids;
    QSet<QString> seen;
    for (const auto* downloader : downloaders_) {
        for (const auto& id : downloader->updatable_ids(mods)) {
            if (!seen.contains(id)) {
                seen.insert(id);
                ids.append(id);
            }
        }
    }
    return ids;
}

QHash<QString, QString> DownloadManager::names_by_id(const QList<ListedModPtr>& mods) const {
    QHash<QString, QString> names;
    for (const auto& mod : mods) {
        if (auto id = updatable_id(*mod)) {
            names.insert(*id, mod->name);
        }
    }
    return names;
}

std::optional<double> DownloadManager::last_synced(const ListedMod& mod) const {
    const Downloader* source = source_of(mod);
    return source ? source->last_synced(mod) : std::nullopt;
}

QCoro::Task<Result<DownloadResult>> DownloadManager::download_mods(QStringList mod_ids) {
    // Ids are grouped by owning downloader in first-seen order; duplicates are dropped.
    std::vector<std::pair<Downloader*, QStringList>> groups;
    QSet<QString> seen;
    for (const auto& mod_id : mod_ids) {
        if (seen.contains(mod_id)) {
            continue;
        }
        seen.insert(mod_id);

        Downloader* owner = nullptr;
        for (auto* downloader : downloaders_) {
            if (downloader->accepts(mod_id)) {
                owner = downloader;
                break;
            }
        }
        if (!owner) {
            co_return std::unexpected(Error{QStringLiteral("No download source handles '%1'.").arg(mod_id)});
        }
        auto group = std::ranges::find(groups, owner, &std::pair<Downloader*, QStringList>::first);
        if (group == groups.end()) {
            groups.emplace_back(owner, QStringList{});
            group = std::prev(groups.end());
        }
        group->second.append(mod_id);
    }
    if (groups.empty()) {
        co_return std::unexpected(Error{QStringLiteral("No mods selected for download.")});
    }
    if (is_downloading()) {
        co_return std::unexpected(Error{QStringLiteral("A download is already running.")});
    }

    cancelled_ = false;
    DownloadResult total;
    for (auto& [downloader, group] : groups) {
        if (cancelled_) {
            break;
        }
        auto result = co_await downloader->download_mods(group);
        if (!result) {
            co_return std::unexpected(result.error());
        }
        total.succeeded += result->succeeded;
        total.failed += result->failed;
        total.changed += result->changed;
    }
    co_return total;
}

bool DownloadManager::cancelled() const noexcept { return cancelled_; }

void DownloadManager::cancel() {
    cancelled_ = true;
    for (auto* downloader : downloaders_) {
        if (downloader->is_downloading()) {
            downloader->cancel();
        }
    }
}

}  // namespace px::modrim
