#include "ui/plugins/steam_workshop/download_sidebar.hpp"

#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>

#include "ui/theme/constants.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {

DownloadSidebar::DownloadSidebar(QQmlEngine* engine, QWidget* parent)
    : QWidget(parent),
      m_model(new DownloadQueueModel(this)),
      m_qml(new QQuickWidget(engine, this)) {
  setObjectName(QStringLiteral("downloadSidebar"));
  setFixedWidth(kSidebarWidth);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  m_qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
  m_qml->setClearColor(paletteColor(QStringLiteral("ELEVATE_2")));
  m_qml->rootContext()->setContextProperty(QStringLiteral("downloadSidebar"),
                                           this);
  m_qml->rootContext()->setContextProperty(QStringLiteral("downloadQueueModel"),
                                           m_model);
  m_qml->setSource(
      QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/DownloadSidebar.qml")));
  layout->addWidget(m_qml);
}

void DownloadSidebar::downloadRequested() { emit downloadTriggered(); }
void DownloadSidebar::stopRequested() { emit stopTriggered(); }
void DownloadSidebar::removeItem(const QString& id) { emit itemRemoved(id); }
void DownloadSidebar::clearQueue() { emit clearRequested(); }
void DownloadSidebar::setDownloadEnabled(bool enabled) {
  if (auto* root = m_qml->rootObject())
    root->setProperty("downloadEnabled", enabled);
}
void DownloadSidebar::syncFrom(const CheckedDownloads& checked,
                               const QHash<QString, QString>& statuses) {
  m_model->syncFrom(checked, statuses);
}
void DownloadSidebar::updateStatus(const QString& id, const QString& status,
                                   qint64 done, qint64 total) {
  m_model->updateStatus(id, status, done, total);
}

}  // namespace px::modrim
