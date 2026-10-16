#include "core/downloads/downloader.h"

#include <utility>

#include "core/downloads/manager.h"

namespace px::modrim {

Downloader::Downloader(QString name, QString label, DownloadManager& manager)
    : Plugin(std::move(name), QStringList{kDownloadsManagerName}),
      label_(std::move(label)),
      manager_(manager) {}

const QString& Downloader::label() const noexcept { return label_; }

void Downloader::setup(CoreContext&) { manager_.register_downloader(*this); }

QStringList Downloader::updatable_ids(const QList<ListedModPtr>& mods) const {
    QStringList ids;
    QSet<QString> seen;
    for (const auto& mod : mods) {
        auto id = updatable_id(*mod);
        if (id && !seen.contains(*id)) {
            seen.insert(*id);
            ids.append(std::move(*id));
        }
    }
    return ids;
}

}  // namespace px::modrim
