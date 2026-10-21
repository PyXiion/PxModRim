#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

#include <QCoro/QCoroTask>

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
  // `label` is the human-readable source name shown in the UI, e.g. "Steam
  // Workshop".
  Downloader(QString name, QString label, DownloadManager& manager);

  const QString& label() const noexcept;

  void setup(CoreContext& ctx) override;

  // Whether a batch is currently downloading.
  virtual bool isDownloading() const = 0;
  // Ids of the batch currently downloading.
  virtual QSet<QString> activeIds() const = 0;
  // Whether `mod_id` belongs to this source.
  virtual bool accepts(const QString& modId) const = 0;
  // Id of `mod` in this source if PxModRim downloaded it from here.
  virtual std::optional<QString> updatableId(const ListedMod& mod) const = 0;
  // Unix time of the last successful sync of `mod`, if known.
  virtual std::optional<double> lastSynced(const ListedMod& mod) const = 0;
  // Downloads or updates `mod_ids`. Per-item failures are reported in the
  // result; an error means the whole batch was refused.
  virtual QCoro::Task<Result<DownloadResult>> downloadMods(
      QStringList modIds) = 0;
  virtual void cancel() = 0;

 protected:
  DownloadManager& manager() const noexcept { return m_manager; }

 private:
  QString m_label;
  DownloadManager& m_manager;
};

}  // namespace px::modrim
