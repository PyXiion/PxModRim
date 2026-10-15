#include "ui/plugins/steam_workshop/download_sidebar.h"

#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>

#include "ui/theme/constants.h"
#include "ui/theme/palette.h"

namespace px::ui {

DownloadSidebar::DownloadSidebar(QQmlEngine* engine, QWidget* parent)
    : QWidget(parent), model_(new DownloadQueueModel(this)), qml_(new QQuickWidget(engine, this)) {
    setObjectName(QStringLiteral("downloadSidebar"));
    setFixedWidth(SIDEBAR_WIDTH);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml_->setClearColor(paletteColor(QStringLiteral("ELEVATE_2")));
    qml_->rootContext()->setContextProperty(QStringLiteral("downloadSidebar"), this);
    qml_->rootContext()->setContextProperty(QStringLiteral("downloadQueueModel"), model_);
    qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/DownloadSidebar.qml")));
    layout->addWidget(qml_);
}

void DownloadSidebar::downloadRequested() { emit download_requested(); }
void DownloadSidebar::stopRequested() { emit stop_requested(); }
void DownloadSidebar::removeItem(const QString& id) { emit item_removed(id); }
void DownloadSidebar::clearQueue() { emit clear_requested(); }
void DownloadSidebar::set_download_enabled(bool enabled) {
    if (auto* root = qml_->rootObject()) root->setProperty("downloadEnabled", enabled);
}
void DownloadSidebar::sync_from(const CheckedDownloads& checked, const QHash<QString, QString>& statuses) {
    model_->sync_from(checked, statuses);
}
void DownloadSidebar::update_status(const QString& id, const QString& status, qint64 done, qint64 total) {
    model_->update_status(id, status, done, total);
}

}  // namespace px::ui
