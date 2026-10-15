#include "ui/plugins/downloads/view.h"

#include <QQmlContext>
#include <QQuickWidget>
#include <spdlog/spdlog.h>

#include "ui/theme/palette.h"

namespace px::ui {

DownloadsViewPanel::DownloadsViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : BaseViewPanel(ctx, engine, parent, app), downloads_(ctx.downloads()), model_(new DownloadsModel(this)) {
    auto* qml = new QQuickWidget(engine, this);
    qml->setObjectName(QStringLiteral("downloadsView"));
    qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml->setClearColor(paletteColor(QStringLiteral("ELEVATE_0")));
    qml->rootContext()->setContextProperty(QStringLiteral("downloadsPanel"), this);
    qml->rootContext()->setContextProperty(QStringLiteral("downloadsModel"), model_);
    qml->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/Downloads.qml")));
    root_->addWidget(qml, 1);
    model_->set_busy(downloads_.is_downloading());
    batch_id_ = downloads_.batch_started.connect([this](const QStringList& ids) {
        model_->begin(ids, downloads_.names_by_id(ctx_.allMods().values()));
    });
    status_id_ = downloads_.download_item_status_changed.connect([this](const core::DownloadItemStatus& status) { model_->apply(status); });
    title_id_ = downloads_.download_item_titled.connect([this](const core::DownloadItemTitle& item) { model_->set_title(item); });
    phase_id_ = downloads_.download_phase_changed.connect([this](const QString& phase) { model_->set_phase(phase); });
    finished_id_ = downloads_.download_finished.connect([this](const core::DownloadResult& result) { model_->finish(result); });
    busy_id_ = downloads_.busy_changed.connect([this](bool busy) { model_->set_busy(busy); });
}

DownloadsViewPanel::~DownloadsViewPanel() {
    downloads_.batch_started.disconnect(batch_id_);
    downloads_.download_item_status_changed.disconnect(status_id_);
    downloads_.download_item_titled.disconnect(title_id_);
    downloads_.download_phase_changed.disconnect(phase_id_);
    downloads_.download_finished.disconnect(finished_id_);
    downloads_.busy_changed.disconnect(busy_id_);
}

void DownloadsViewPanel::stop() { downloads_.cancel(); }

QCoro::QmlTask DownloadsViewPanel::retryFailed() { return retry_failed(); }

QCoro::Task<void> DownloadsViewPanel::retry_failed() {
    auto ids = model_->failed_ids();
    if (ids.isEmpty()) co_return;
    auto result = co_await downloads_.download_mods(std::move(ids));
    if (!result) spdlog::warn("downloads retry not started: {}", result.error().message.toStdString());
}

}  // namespace px::ui
