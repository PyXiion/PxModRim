#pragma once

#include <QCoro/QCoroQmlTask>
#include <QCoro/QCoroTask>

#include "core/downloads/manager.hpp"
#include "ui/plugins/downloads/model.hpp"
#include "ui/views/base.hpp"

namespace px::modrim {

class DownloadsViewPanel final : public BaseViewPanel {
  Q_OBJECT
 public:
  static constexpr RailViewMeta kRailMeta{QLatin1StringView("downloads"),
                                          QLatin1StringView("download"),
                                          QLatin1StringView("Downloads"), true};
  DownloadsViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
                     AppContext& app);
  ~DownloadsViewPanel() override;
  Q_INVOKABLE void stop();
  Q_INVOKABLE QCoro::QmlTask retryFailed();

 private:
  QCoro::Task<void> retryFailedAsync();
  DownloadManager& m_downloads;
  DownloadsModel* m_model;
  Event<QStringList>::Id m_batchId;
  Event<DownloadItemStatus>::Id m_statusId;
  Event<DownloadItemTitle>::Id m_titleId;
  Event<QString>::Id m_phaseId;
  Event<DownloadResult>::Id m_finishedId;
  Event<bool>::Id m_busyId;
};

}  // namespace px::modrim
