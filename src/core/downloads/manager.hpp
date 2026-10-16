#pragma once

#include <optional>
#include <vector>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/downloads/downloader.hpp"
#include "core/downloads/types.hpp"
#include "core/error.hpp"
#include "core/events.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/plugin.hpp"

namespace px::modrim {

// The one download entry point for the UI; routes to registered downloaders.
// With no downloader registered nothing is updatable and `available()` is false,
// which is how the UI hides every download control.
class DownloadManager : public Plugin {
public:
    DownloadManager();

    // Called by each downloader from its `setup`. The downloader must outlive the manager's use of it.
    void registerDownloader(Downloader& downloader);

    const std::vector<Downloader*>& downloaders() const noexcept;
    bool available() const noexcept;
    bool isDownloading() const;
    QSet<QString> activeIds() const;

    // The downloader that can update `mod`, or nullptr.
    Downloader* sourceOf(const ListedMod& mod) const;
    std::optional<QString> updatableId(const ListedMod& mod) const;
    QStringList updatableIds(const QList<ListedModPtr>& mods) const;
    // Display names of installed `mods`, keyed by their source id.
    QHash<QString, QString> namesById(const QList<ListedModPtr>& mods) const;
    std::optional<double> lastSynced(const ListedMod& mod) const;

    // Downloads `mod_ids`, one downloader after another. Errors before anything starts
    // (unclaimed id, nothing selected, already running) are returned without downloading.
    QCoro::Task<Result<DownloadResult>> downloadMods(QStringList modIds);

    bool cancelled() const noexcept;
    void cancel();

    Event<QString> statusMessageChanged;
    Event<DownloadProgress> downloadProgress;
    Event<DownloadItemStatus> downloadItemStatusChanged;
    Event<DownloadItemTitle> downloadItemTitled;
    Event<QString> downloadPhaseChanged;
    Event<DownloadResult> downloadFinished;
    Event<bool> busyChanged;
    Event<QStringList> batchStarted;

private:
    void onBusy(bool busy);

    std::vector<Downloader*> m_downloaders;
    bool m_cancelled = false;
};

}  // namespace px::modrim
