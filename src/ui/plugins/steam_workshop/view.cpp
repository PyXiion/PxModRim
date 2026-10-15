#include "ui/plugins/steam_workshop/view.h"

#ifdef PX_HAS_STEAM

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

#include "ui/plugins/steam_workshop/bridge_obj.h"
#include "ui/plugins/steam_workshop/download_sidebar.h"
#include "ui/theme/palette.h"

namespace px::ui {
namespace {
const QUrl workshop_url(QStringLiteral("https://steamcommunity.com/workshop/browse/?appid=294100"));

QString json_argument(const QJsonValue& value) {
    const auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.constData() + 1, json.size() - 2);
}

QString injection() {
    QFile js(QStringLiteral(":/qt/qml/PxModRim/plugins/steam_workshop/inject.js"));
    if (!js.open(QIODevice::ReadOnly)) {
        spdlog::warn("Steam Workshop injection bundle is absent; browser download controls are unavailable");
        return {};
    }
    QFile css(QStringLiteral(":/qt/qml/PxModRim/plugins/steam_workshop/inject.css"));
    QString styles;
    if (css.open(QIODevice::ReadOnly)) styles = QString::fromUtf8(css.readAll());
    return QStringLiteral("var CSS_STYLES = %1;\n%2").arg(json_argument(styles), QString::fromUtf8(js.readAll()));
}
}

SteamWorkshopViewPanel::SteamWorkshopViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : BaseViewPanel(ctx, engine, parent, app) {
    auto* content = new QWidget(this);
    auto* layout = new QHBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    sidebar_ = new DownloadSidebar(engine, content);
    layout->addWidget(sidebar_);
    qml_ = new QQuickWidget(engine, content);
    qml_->setObjectName(QStringLiteral("workshopView"));
    qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml_->setClearColor(paletteColor(QStringLiteral("ELEVATE_0")));
#ifdef PX_HAS_WEBENGINE
    layout->addWidget(qml_, 1);
#else
    qml_->hide();
    auto* unavailable = new QLabel(QStringLiteral("This build does not include the Steam Workshop web browser."), content);
    unavailable->setAlignment(Qt::AlignCenter);
    unavailable->setWordWrap(true);
    layout->addWidget(unavailable, 1);
#endif
    root_->addWidget(content, 1);
}

SteamWorkshopViewPanel::~SteamWorkshopViewPanel() { teardown(); }

void SteamWorkshopViewPanel::ensure_initialized() {
    if (initialized_) return;
    initialized_ = true;
    plugin_ = static_cast<SteamWorkshopUiPlugin*>(app_.plugins().get(QStringLiteral("steamworkshop")));
    if (plugin_) {
        connect(sidebar_, &DownloadSidebar::download_requested, this, [this] { plugin_->request_download(); });
        connect(sidebar_, &DownloadSidebar::stop_requested, this, [this] { plugin_->stop_download(); });
        connect(sidebar_, &DownloadSidebar::item_removed, this, [this](const QString& id) { plugin_->remove_item(id); });
        connect(sidebar_, &DownloadSidebar::clear_requested, this, [this] { plugin_->clear_queue(); });
        badges_id_ = plugin_->badges_refresh_requested.connect([this](const QStringList& ids) {
            run_js(QStringLiteral("if (window.__pxmSetInstalled) window.__pxmSetInstalled(%1)").arg(json_argument(QJsonArray::fromStringList(ids))));
        });
        active_id_ = plugin_->active_refresh_requested.connect([this](const QStringList& ids) {
            run_js(QStringLiteral("if (window.__pxmSetActive) window.__pxmSetActive(%1)").arg(json_argument(QJsonArray::fromStringList(ids))));
        });
        sidebar_id_ = plugin_->sidebar_sync_requested.connect([this](const SidebarSync& sync) { sidebar_->sync_from(sync.checked_ids, sync.statuses); });
        status_id_ = plugin_->item_status_changed.connect([this](const core::DownloadItemStatus& status) {
            sidebar_->update_status(status.mod_id, status.status, status.bytes_done, status.bytes_total);
        });
        busy_id_ = plugin_->download_busy_changed.connect([this](bool busy) { sidebar_->set_download_enabled(!busy); });
        uncheck_id_ = plugin_->uncheck_mod_requested.connect([this](const QString& id) {
            run_js(QStringLiteral("if (window.__pxmUncheckMod) window.__pxmUncheckMod(%1);").arg(json_argument(id)));
        });
        clear_id_ = plugin_->clear_checked_requested.connect([this](const std::monostate&) {
            run_js(QStringLiteral("if (window.__pxmClearChecked) window.__pxmClearChecked();"));
        });
        sidebar_->set_download_enabled(!plugin_->is_downloading());
        plugin_->sync_all();
    }
#ifdef PX_HAS_WEBENGINE
    auto* bridge = plugin_ ? new PxModRimBridge(*plugin_, qml_) : nullptr;
    if (bridge) bridge->setObjectName(QStringLiteral("workshopBridge"));
    auto* context = qml_->rootContext();
    context->setContextProperty(QStringLiteral("steamWorkshopPanel"), this);
    context->setContextProperty(QStringLiteral("_bridge"), bridge);
    context->setContextProperty(QStringLiteral("_injectCode"), injection());
    qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/SteamWorkshop.qml")));
#endif
}

QObject* SteamWorkshopViewPanel::web() const {
#ifdef PX_HAS_WEBENGINE
    if (initialized_ && qml_->rootObject()) return qml_->rootObject()->findChild<QObject*>(QStringLiteral("workshopWeb"));
#endif
    return nullptr;
}

void SteamWorkshopViewPanel::run_js(const QString& code) {
#ifdef PX_HAS_WEBENGINE
    if (auto* root = qml_->rootObject()) QMetaObject::invokeMethod(root, "runScript", Q_ARG(QVariant, code));
#else
    Q_UNUSED(code);
#endif
}

void SteamWorkshopViewPanel::navigateHome() {
    if (auto* browser = web()) browser->setProperty("url", workshop_url);
}

void SteamWorkshopViewPanel::navigateToUrl(const QString& text) {
    if (text.isEmpty()) return;
    auto url = QUrl::fromUserInput(text);
    if (url.scheme() == QLatin1String("http")) url.setScheme(QStringLiteral("https"));
    if (!url.isValid()) return;
    if (auto* browser = web()) browser->setProperty("url", url);
}

void SteamWorkshopViewPanel::preload() { ensure_initialized(); }
void SteamWorkshopViewPanel::showEvent(QShowEvent* event) {
    BaseViewPanel::showEvent(event);
    ensure_initialized();
}

void SteamWorkshopViewPanel::teardown() {
    if (!initialized_) return;
    if (auto* browser = web()) {
        QMetaObject::invokeMethod(browser, "stop");
        browser->setProperty("url", QUrl(QStringLiteral("about:blank")));
    }
    qml_->setSource({});
    if (auto* bridge = qml_->findChild<PxModRimBridge*>(QStringLiteral("workshopBridge"))) delete bridge;
    disconnect(sidebar_, nullptr, this, nullptr);
    if (plugin_) {
        plugin_->badges_refresh_requested.disconnect(badges_id_);
        plugin_->active_refresh_requested.disconnect(active_id_);
        plugin_->sidebar_sync_requested.disconnect(sidebar_id_);
        plugin_->item_status_changed.disconnect(status_id_);
        plugin_->download_busy_changed.disconnect(busy_id_);
        plugin_->uncheck_mod_requested.disconnect(uncheck_id_);
        plugin_->clear_checked_requested.disconnect(clear_id_);
    }
    plugin_ = nullptr;
    initialized_ = false;
}

}  // namespace px::ui
#endif
