#include "ui/plugins/downloads/view.hpp"

#include <QQmlContext>
#include <QQuickWidget>
#include <spdlog/spdlog.h>

#include "ui/theme/palette.hpp"

namespace px::modrim {

DownloadsViewPanel::DownloadsViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : BaseViewPanel(ctx, engine, parent, app), m_downloads(ctx.downloads()), m_model(new DownloadsModel(this)) {
    auto* qml = new QQuickWidget(engine, this);
    qml->setObjectName(QStringLiteral("downloadsView"));
    qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml->setClearColor(paletteColor(QStringLiteral("ELEVATE_0")));
    qml->rootContext()->setContextProperty(QStringLiteral("downloadsPanel"), this);
    qml->rootContext()->setContextProperty(QStringLiteral("downloadsModel"), m_model);
    qml->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/Downloads.qml")));
    m_root->addWidget(qml, 1);
    m_model->setBusy(m_downloads.isDownloading());
    m_batchId = m_downloads.batchStarted.connect([this](const QStringList& ids) {
        m_model->begin(ids, m_downloads.namesById(m_ctx.allMods().values()));
    });
    m_statusId = m_downloads.downloadItemStatusChanged.connect([this](const DownloadItemStatus& status) { m_model->apply(status); });
    m_titleId = m_downloads.downloadItemTitled.connect([this](const DownloadItemTitle& item) { m_model->setTitle(item); });
    m_phaseId = m_downloads.downloadPhaseChanged.connect([this](const QString& phase) { m_model->setPhase(phase); });
    m_finishedId = m_downloads.downloadFinished.connect([this](const DownloadResult& result) { m_model->finish(result); });
    m_busyId = m_downloads.busyChanged.connect([this](bool busy) { m_model->setBusy(busy); });
}

DownloadsViewPanel::~DownloadsViewPanel() {
    // The QML bindings read downloadsModel, which is a child of this panel and would otherwise be
    // destroyed before the view that binds to it.
    delete findChild<QQuickWidget*>(QStringLiteral("downloadsView"));
    m_downloads.batchStarted.disconnect(m_batchId);
    m_downloads.downloadItemStatusChanged.disconnect(m_statusId);
    m_downloads.downloadItemTitled.disconnect(m_titleId);
    m_downloads.downloadPhaseChanged.disconnect(m_phaseId);
    m_downloads.downloadFinished.disconnect(m_finishedId);
    m_downloads.busyChanged.disconnect(m_busyId);
}

void DownloadsViewPanel::stop() { m_downloads.cancel(); }

QCoro::QmlTask DownloadsViewPanel::retryFailed() { return retryFailedAsync(); }

QCoro::Task<void> DownloadsViewPanel::retryFailedAsync() {
    auto ids = m_model->failedIds();
    if (ids.isEmpty()) co_return;
    auto result = co_await m_downloads.downloadMods(std::move(ids));
    if (!result) spdlog::warn("downloads retry not started: {}", result.error().message.toStdString());
}

}  // namespace px::modrim
