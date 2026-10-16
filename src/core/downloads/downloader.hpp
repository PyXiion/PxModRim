#pragma once

#include <optional>

#include <QCoro/QCoroTask>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/downloads/types.hpp"
#include "core/error.hpp"
#include "core/events.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/plugin.hpp"

namespace px::modrim {

inline const QString kDownloadsManagerName = QStringLiteral("downloads");

class DownloadManager;

// A source that downloads and updates mods into the local mods folder.
// Constructing a downloader hands it to `manager`; `setup` registers it, so the
// UI only talks to the manager and a downloader can be left out freely.
class Downloader : public Plugin {
public:
    // `label` is the human-readable source name shown in the UI, e.g. "Steam Workshop".
    Downloader(QString name, QString label, DownloadManager& manager);

    const QString& label() const noexcept;

    void setup(CoreContext& ctx) override;

    // Whether a batch is currently downloading.
    virtual bool is_downloading() const = 0;
    // Ids of the batch currently downloading.
    virtual QSet<QString> active_ids() const = 0;
    // Whether `mod_id` belongs to this source.
    virtual bool accepts(const QString& mod_id) const = 0;
    // Id of `mod` in this source if PxModRim downloaded it from here.
    virtual std::optional<QString> updatable_id(const ListedMod& mod) const = 0;
    // Unix time of the last successful sync of `mod`, if known.
    virtual std::optional<double> last_synced(const ListedMod& mod) const = 0;
    // Downloads or updates `mod_ids`. Per-item failures are reported in the result;
    // an error means the whole batch was refused.
    virtual QCoro::Task<Result<DownloadResult>> download_mods(QStringList mod_ids) = 0;
    virtual void cancel() = 0;

    // Distinct updatable ids among `mods`, in first-seen order.
    QStringList updatable_ids(const QList<ListedModPtr>& mods) const;

    Event<QString> status_message_changed;
    Event<DownloadProgress> download_progress;
    Event<DownloadItemStatus> download_item_status_changed;
    Event<DownloadItemTitle> download_item_titled;
    // "login" -> "query" (item details) -> "run" (all items resolved).
    Event<QString> download_phase_changed;
    Event<DownloadResult> download_finished;
    Event<bool> busy_changed;
    Event<QStringList> batch_started;

private:
    QString label_;
    DownloadManager& manager_;
};

}  // namespace px::modrim
