#include "core/downloads/downloader.hpp"

#include <utility>

#include "core/downloads/manager.hpp"

namespace px::modrim {

Downloader::Downloader(QString name, QString label, DownloadManager& manager)
    : Plugin(std::move(name), QStringList{kDownloadsManagerName}),
      m_label(std::move(label)),
      m_manager(manager) {}

const QString& Downloader::label() const noexcept { return m_label; }

void Downloader::setup(CoreContext&) { m_manager.registerDownloader(*this); }

}  // namespace px::modrim
