#pragma once

#include <QCoro/QCoroTask>
#include <QCoro/QCoroQmlTask>

#include "core/downloads/manager.h"
#include "ui/plugins/downloads/model.h"
#include "ui/views/base.h"

namespace px::ui {

class DownloadsViewPanel final : public BaseViewPanel {
    Q_OBJECT
public:
    static constexpr RailViewMeta kRailMeta{QLatin1StringView("downloads"), QLatin1StringView("download"), QLatin1StringView("Downloads"), true};
    DownloadsViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app);
    ~DownloadsViewPanel() override;
    Q_INVOKABLE void stop();
    Q_INVOKABLE QCoro::QmlTask retryFailed();

private:
    QCoro::Task<void> retry_failed();
    core::DownloadManager& downloads_;
    DownloadsModel* model_;
    core::Event<QStringList>::Id batch_id_;
    core::Event<core::DownloadItemStatus>::Id status_id_;
    core::Event<core::DownloadItemTitle>::Id title_id_;
    core::Event<QString>::Id phase_id_;
    core::Event<core::DownloadResult>::Id finished_id_;
    core::Event<bool>::Id busy_id_;
};

}  // namespace px::ui
