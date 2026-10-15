#pragma once

#include <optional>
#include <vector>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/downloads/downloader.h"
#include "core/downloads/types.h"
#include "core/error.h"
#include "core/events.h"
#include "core/models/metadata/structures.h"
#include "core/plugin.h"

namespace px::core {

// The one download entry point for the UI; routes to registered downloaders.
// With no downloader registered nothing is updatable and `available()` is false,
// which is how the UI hides every download control.
class DownloadManager : public Plugin {
public:
    DownloadManager();

    // Called by each downloader from its `setup`. The downloader must outlive the manager's use of it.
    void register_downloader(Downloader& downloader);

    const std::vector<Downloader*>& downloaders() const noexcept;
    bool available() const noexcept;
    bool is_downloading() const;
    QSet<QString> active_ids() const;

    // The downloader that can update `mod`, or nullptr.
    Downloader* source_of(const ListedMod& mod) const;
    std::optional<QString> updatable_id(const ListedMod& mod) const;
    QStringList updatable_ids(const QList<ListedModPtr>& mods) const;
    // Display names of installed `mods`, keyed by their source id.
    QHash<QString, QString> names_by_id(const QList<ListedModPtr>& mods) const;
    std::optional<double> last_synced(const ListedMod& mod) const;

    // Downloads `mod_ids`, one downloader after another. Errors before anything starts
    // (unclaimed id, nothing selected, already running) are returned without downloading.
    QCoro::Task<Result<DownloadResult>> download_mods(QStringList mod_ids);

    bool cancelled() const noexcept;
    void cancel();

    Event<QString> status_message_changed;
    Event<DownloadProgress> download_progress;
    Event<DownloadItemStatus> download_item_status_changed;
    Event<DownloadItemTitle> download_item_titled;
    Event<QString> download_phase_changed;
    Event<DownloadResult> download_finished;
    Event<bool> busy_changed;
    Event<QStringList> batch_started;

private:
    void on_busy(bool busy);

    std::vector<Downloader*> downloaders_;
    bool cancelled_ = false;
};

}  // namespace px::core
