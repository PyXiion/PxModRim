#include "ui/plugins/steam_workshop/view.hpp"

#ifdef PX_HAS_STEAM

#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QShowEvent>

#include <spdlog/spdlog.h>

#include "ui/plugins/steam_workshop/bridge_obj.hpp"
#include "ui/plugins/steam_workshop/download_sidebar.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {
namespace {
const QUrl kWorkshopUrl(
    QStringLiteral("https://steamcommunity.com/workshop/browse/?appid=294100"));

QString jsonArgument(const QJsonValue& value) {
  const auto json =
      QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
  return QString::fromUtf8(json.constData() + 1, json.size() - 2);
}

QString injection() {
  QFile js(
      QStringLiteral(":/qt/qml/PxModRim/plugins/steam_workshop/inject.js"));
  if (!js.open(QIODevice::ReadOnly)) {
    spdlog::warn(
        "Steam Workshop injection bundle is absent; browser download controls "
        "are unavailable");
    return {};
  }
  QFile css(
      QStringLiteral(":/qt/qml/PxModRim/plugins/steam_workshop/inject.css"));
  QString styles;
  if (css.open(QIODevice::ReadOnly)) styles = QString::fromUtf8(css.readAll());
  return QStringLiteral("var CSS_STYLES = %1;\n%2")
      .arg(jsonArgument(styles), QString::fromUtf8(js.readAll()));
}
}  // namespace

SteamWorkshopViewPanel::SteamWorkshopViewPanel(CoreContext& ctx,
                                               QQmlEngine* engine,
                                               QWidget* parent, AppContext& app)
    : BaseViewPanel(ctx, engine, parent, app) {
  auto* content = new QWidget(this);
  auto* layout = new QHBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  m_sidebar = new DownloadSidebar(engine, content);
  layout->addWidget(m_sidebar);
  m_qml = new QQuickWidget(engine, content);
  m_qml->setObjectName(QStringLiteral("workshopView"));
  m_qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
  m_qml->setClearColor(paletteColor(QStringLiteral("ELEVATE_0")));
#ifdef PX_HAS_WEBENGINE
  layout->addWidget(m_qml, 1);
#else
  m_qml->hide();
  auto* unavailable = new QLabel(
      QStringLiteral(
          "This build does not include the Steam Workshop web browser."),
      content);
  unavailable->setAlignment(Qt::AlignCenter);
  unavailable->setWordWrap(true);
  layout->addWidget(unavailable, 1);
#endif
  m_root->addWidget(content, 1);
}

SteamWorkshopViewPanel::~SteamWorkshopViewPanel() { teardown(); }

void SteamWorkshopViewPanel::ensureInitialized() {
  if (m_initialized) return;
  m_initialized = true;
  m_plugin = static_cast<SteamWorkshopUiPlugin*>(
      m_app.plugins().get(QStringLiteral("steamworkshop")));
  if (m_plugin) {
    connect(m_sidebar, &DownloadSidebar::downloadTriggered, this,
            [this] { m_plugin->requestDownload(); });
    connect(m_sidebar, &DownloadSidebar::stopTriggered, this,
            [this] { m_plugin->stopDownload(); });
    connect(m_sidebar, &DownloadSidebar::itemRemoved, this,
            [this](const QString& id) { m_plugin->removeItem(id); });
    connect(m_sidebar, &DownloadSidebar::clearRequested, this,
            [this] { m_plugin->clearQueue(); });
    m_badgesId = m_plugin->badgesRefreshRequested.connect(
        [this](const QStringList& ids) {
          runJs(
              QStringLiteral(
                  "if (window.__pxmSetInstalled) window.__pxmSetInstalled(%1)")
                  .arg(jsonArgument(QJsonArray::fromStringList(ids))));
        });
    m_activeId = m_plugin->activeRefreshRequested.connect(
        [this](const QStringList& ids) {
          runJs(QStringLiteral(
                    "if (window.__pxmSetActive) window.__pxmSetActive(%1)")
                    .arg(jsonArgument(QJsonArray::fromStringList(ids))));
        });
    m_sidebarId =
        m_plugin->sidebarSyncRequested.connect([this](const SidebarSync& sync) {
          m_sidebar->syncFrom(sync.checkedIds, sync.statuses);
        });
    m_statusId = m_plugin->itemStatusChanged.connect(
        [this](const DownloadItemStatus& status) {
          m_sidebar->updateStatus(status.modId, status.status, status.bytesDone,
                                  status.bytesTotal);
        });
    m_busyId = m_plugin->downloadBusyChanged.connect(
        [this](bool busy) { m_sidebar->setDownloadEnabled(!busy); });
    m_uncheckId =
        m_plugin->uncheckModRequested.connect([this](const QString& id) {
          runJs(QStringLiteral(
                    "if (window.__pxmUncheckMod) window.__pxmUncheckMod(%1);")
                    .arg(jsonArgument(id)));
        });
    m_clearId =
        m_plugin->clearCheckedRequested.connect([this](const std::monostate&) {
          runJs(QStringLiteral(
              "if (window.__pxmClearChecked) window.__pxmClearChecked();"));
        });
    m_sidebar->setDownloadEnabled(!m_plugin->isDownloading());
    m_plugin->syncAll();
  }
#ifdef PX_HAS_WEBENGINE
  auto* bridge = m_plugin ? new PxModRimBridge(*m_plugin, m_qml) : nullptr;
  if (bridge) bridge->setObjectName(QStringLiteral("workshopBridge"));
  auto* context = m_qml->rootContext();
  context->setContextProperty(QStringLiteral("steamWorkshopPanel"), this);
  context->setContextProperty(QStringLiteral("_bridge"), bridge);
  context->setContextProperty(QStringLiteral("_injectCode"), injection());
  m_qml->setSource(
      QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/SteamWorkshop.qml")));
  if (bridge) bridge->setPage(web());
#endif
}

QObject* SteamWorkshopViewPanel::web() const {
#ifdef PX_HAS_WEBENGINE
  if (m_initialized && m_qml->rootObject())
    return m_qml->rootObject()->findChild<QObject*>(
        QStringLiteral("workshopWeb"));
#endif
  return nullptr;
}

void SteamWorkshopViewPanel::runJs(const QString& code) {
#ifdef PX_HAS_WEBENGINE
  if (auto* root = m_qml->rootObject())
    QMetaObject::invokeMethod(root, "runScript", Q_ARG(QVariant, code));
#else
  Q_UNUSED(code);
#endif
}

void SteamWorkshopViewPanel::navigateHome() {
  if (auto* browser = web()) browser->setProperty("url", kWorkshopUrl);
}

bool SteamWorkshopViewPanel::isSteamOrigin(const QUrl& url) {
  if (!url.isValid() || url.scheme() != QLatin1String("https")) return false;
  const auto host = url.host();
  return host == QLatin1String("steamcommunity.com") ||
         host.endsWith(QLatin1String(".steamcommunity.com"));
}

void SteamWorkshopViewPanel::navigateToUrl(const QString& text) {
  if (text.isEmpty()) return;
  auto url = QUrl::fromUserInput(text);
  if (!url.isValid()) return;
  if (!isSteamOrigin(url)) {
    QDesktopServices::openUrl(url);
    return;
  }
  if (auto* browser = web()) browser->setProperty("url", url);
}

void SteamWorkshopViewPanel::preload() { ensureInitialized(); }
void SteamWorkshopViewPanel::showEvent(QShowEvent* event) {
  BaseViewPanel::showEvent(event);
  ensureInitialized();
}

void SteamWorkshopViewPanel::teardown() {
  if (!m_initialized) return;
  if (auto* browser = web()) {
    QMetaObject::invokeMethod(browser, "stop");
    browser->setProperty("url", QUrl(QStringLiteral("about:blank")));
  }
  m_qml->setSource({});
  if (auto* bridge =
          m_qml->findChild<PxModRimBridge*>(QStringLiteral("workshopBridge")))
    delete bridge;
  disconnect(m_sidebar, nullptr, this, nullptr);
  if (m_plugin) {
    m_plugin->badgesRefreshRequested.disconnect(m_badgesId);
    m_plugin->activeRefreshRequested.disconnect(m_activeId);
    m_plugin->sidebarSyncRequested.disconnect(m_sidebarId);
    m_plugin->itemStatusChanged.disconnect(m_statusId);
    m_plugin->downloadBusyChanged.disconnect(m_busyId);
    m_plugin->uncheckModRequested.disconnect(m_uncheckId);
    m_plugin->clearCheckedRequested.disconnect(m_clearId);
  }
  m_plugin = nullptr;
  m_initialized = false;
}

}  // namespace px::modrim
#endif
