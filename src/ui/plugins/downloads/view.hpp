#pragma once

#include <QCoro/QCoroTask>
#include <QCoro/QCoroQmlTask>

#include "core/downloads/manager.hpp"
#include "ui/plugins/downloads/model.hpp"
#include "ui/views/base.hpp"

namespace px::modrim {

class DownloadsViewPanel final : public BaseViewPanel {
    Q_OBJECT
public:
    static constexpr RailViewMeta kRailMeta{QLatin1StringView("downloads"), QLatin1StringView("download"), QLatin1StringView("Downloads"), true};
    DownloadsViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app);
    ~DownloadsViewPanel() override;
    Q_INVOKABLE void stop();
    Q_INVOKABLE QCoro::QmlTask retryFailed();

private:
    QCoro::Task<void> retry_failed();
    DownloadManager& downloads_;
    DownloadsModel* model_;
    Event<QStringList>::Id batch_id_;
    Event<DownloadItemStatus>::Id status_id_;
    Event<DownloadItemTitle>::Id title_id_;
    Event<QString>::Id phase_id_;
    Event<DownloadResult>::Id finished_id_;
    Event<bool>::Id busy_id_;
};

}  // namespace px::modrim
