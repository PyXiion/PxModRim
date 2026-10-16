#include "ui/panels/mod_list_panel.hpp"
#include "core/context.hpp"
#include "core/mod_service.hpp"
#include "core/downloads/manager.hpp"
#include "ui/components/icons.hpp"
#include "ui/components/mod_activation.hpp"
#include "ui/components/mod_updates.hpp"
#include "ui/theme/palette.hpp"
#include <QAction>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>
#include <utility>
namespace px::modrim {
ModListPanel::ModListPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_model(new ModListModel({}, &ctx.diagnosticsService(), this)),
      m_proxy(new ModListProxyModel(m_model, this)), m_qml(new QQuickWidget(engine, this)),
      m_compactMode(ctx.config().compact_mod_list), m_highlightTimer(new QTimer(this)) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto* search = new QWidget(this); search->setObjectName(QStringLiteral("searchBox")); search->setFixedHeight(56);
    auto* searchLayout = new QHBoxLayout(search); searchLayout->setContentsMargins(16,8,16,8);
    searchInput = new QLineEdit(search);
    searchInput->setObjectName(QStringLiteral("searchInput"));
    searchInput->setPlaceholderText(QStringLiteral("Search mods, package ID, author…"));
    searchInput->setClearButtonEnabled(true);
    connect(searchInput, &QLineEdit::textChanged, this, &ModListPanel::setSearchFilter);
    if (const auto searchIcon = icon(u"search",16,paletteValue(u"TEXT_DIM"))) searchInput->addAction(*searchIcon,QLineEdit::LeadingPosition);
    searchLayout->addWidget(searchInput); layout->addWidget(search);
    connect(m_model, &ModListModel::activeModsChanged, this, &ModListPanel::activeModsChanged);
    m_qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_qml->setAttribute(Qt::WA_AlwaysStackOnTop,false);
    m_qml->setClearColor(paletteColor(u"ELEVATE_1")); m_qml->setAcceptDrops(true);
    m_qml->rootContext()->setContextProperty(QStringLiteral("modListPanel"),this);
    m_qml->rootContext()->setContextProperty(QStringLiteral("modListModel"),m_proxy);
    m_qml->rootContext()->setContextProperty(QStringLiteral("modListHasFocus"),false);
    m_qml->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/ModList.qml")));
    layout->addWidget(m_qml);
    m_qml->setFocusPolicy(Qt::StrongFocus); m_qml->installEventFilter(this);
    m_highlightTimer->setSingleShot(true); m_highlightTimer->setInterval(1200);
    connect(m_highlightTimer, &QTimer::timeout, this, [this] { setHighlighted({}); });
    m_activeConnection = m_ctx.activeStateChanged().connect([this](const auto& active) { coreStateChanged(active); });
    m_configConnection = m_ctx.configChanged().connect([this](const auto& cfg) { setCompactMode(cfg.compact_mod_list); });
    m_modsConnection = m_ctx.modService().modsChanged.connect([this](const auto&) { refresh(); });
}
ModListPanel::~ModListPanel() {
    m_ctx.activeStateChanged().disconnect(m_activeConnection);
    m_ctx.configChanged().disconnect(m_configConnection);
    m_ctx.modService().modsChanged.disconnect(m_modsConnection);
    // QML bindings refer to the models and panel during destruction.
    delete m_qml;
}
void ModListPanel::loadMods(const QHash<QString, ListedModPtr>& mods, const QStringList& active) { m_model->loadMods(mods,active); }
QStringList ModListPanel::activeUuids() const { return m_model->activeUuids(); }
void ModListPanel::setSidebarFilter(std::optional<QSet<QString>> uuids) { m_proxy->setSidebarFilter(std::move(uuids)); }
void ModListPanel::setSearchFilter(const QString& text) { m_proxy->setSearchFilter(text); }
void ModListPanel::setCompactMode(bool enabled) {
    if (m_compactMode == enabled) return;
    m_compactMode = enabled; emit compactModeChanged(enabled);
}
bool ModListPanel::isHighlighted(const QString& uuid) const { return m_highlighted.contains(uuid); }
void ModListPanel::modSelected(const QString& uuid) { emit modPicked(uuid); }
int ModListPanel::proxyToSourceRow(int row) const { return m_proxy->mapToSource(m_proxy->index(row)).row(); }
QString ModListPanel::uuidAt(int row) const { const auto* item = m_model->getItem(proxyToSourceRow(row)); return item ? item->uuid : QString{}; }
int ModListPanel::rowForUuid(const QString& uuid) const { return m_proxy->proxyRowForUuid(uuid); }
bool ModListPanel::isChecked(int row) const { const auto* item = m_model->getItem(proxyToSourceRow(row)); return item && item->checked; }
bool ModListPanel::selectUuid(const QString& uuid) {
    const int row = rowForUuid(uuid);
    auto* root = m_qml->rootObject();
    if (row < 0 || !root) return false;
    return QMetaObject::invokeMethod(root,"revealRow",Qt::DirectConnection,Q_ARG(QVariant,QVariant(row)),Q_ARG(QVariant,QVariant(uuid)));
}
void ModListPanel::clearSelection() {
    auto* root = m_qml->rootObject();
    auto* view = root ? root->findChild<QObject*>(QStringLiteral("listView")) : nullptr;
    if (view) {
        view->setProperty("currentIndex",-1); view->setProperty("anchorIndex",-1);
        view->setProperty("selectedIndices",QVariantList{}); view->setProperty("selectedUuids",QVariantList{});
        view->setProperty("currentUuid",QString{}); view->setProperty("anchorUuid",QString{});
    }
    emit selectionUpdated({}); emit modPicked({});
}
QCoro::Task<void> ModListPanel::refresh() {
    m_model->updateProviderColors(m_ctx.modService().providerColors());
    m_model->loadMods(m_ctx.allMods(),m_ctx.activeUuids());
    QPointer<ModListPanel> self(this);
    const auto impacts = co_await m_ctx.modService().startupImpact().getAllAverages();
    if (self && impacts) self->m_model->setStartupImpact(*impacts);
}
QCoro::QmlTask ModListPanel::toggleChecked(const QVariantList& rows) {
    QList<int> selected; selected.reserve(rows.size());
    for (const auto& row : rows) selected.append(row.toInt());
    return toggleRows(std::move(selected));
}
QCoro::Task<void> ModListPanel::toggleRows(QList<int> rows) {
    QStringList selected;
    for (const int row : rows) { const auto uuid = uuidAt(row); if (!uuid.isEmpty()) selected.append(uuid); }
    if (selected.isEmpty()) co_return;
    const QSet<QString> previous(m_ctx.activeUuids().begin(),m_ctx.activeUuids().end());
    QPointer<ModListPanel> self(this);
    co_await toggleMods(m_ctx,this,selected);
    if (!self) co_return;
    QStringList newlyEnabled;
    for (const auto& uuid : selected) if (!previous.contains(uuid) && m_ctx.activeUuids().contains(uuid)) newlyEnabled.append(uuid);
    if (!newlyEnabled.isEmpty()) highlightRows(newlyEnabled);
}
void ModListPanel::setHighlighted(const QStringList& uuids) {
    m_highlighted = QSet<QString>(uuids.begin(),uuids.end()); ++m_highlightRevision; emit highlightedChanged();
}
void ModListPanel::highlightRows(const QStringList& uuids) {
    int last = -1;
    for (const auto& uuid : uuids) if (const int row = rowForUuid(uuid); row >= 0) last = row;
    auto* root = m_qml->rootObject();
    if (last < 0 || !root) return;
    if (auto* view = root->findChild<QObject*>(QStringLiteral("listView")))
        QMetaObject::invokeMethod(view,"positionViewAtIndex",Qt::DirectConnection,Q_ARG(int,last),Q_ARG(int,4));
    setHighlighted(uuids); m_highlightTimer->start();
}
void ModListPanel::showContextMenu(const QVariantList& values) {
    if (!m_ctx.downloads().available()) return;
    QStringList uuids; uuids.reserve(values.size());
    for (const auto& value : values) uuids.append(value.toString());
    auto* menu = new QMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose); menu->setToolTipsVisible(true);
    addUpdateAction(menu,m_ctx,uuids,[this,uuids] { updateMods(m_ctx,uuids,this); });
    menu->popup(QCursor::pos());
}
void ModListPanel::coreStateChanged(const QStringList& uuids) {
    const auto currentList = m_model->activeUuids();
    const QSet<QString> current(currentList.begin(),currentList.end()), next(uuids.begin(),uuids.end());
    m_model->setCheckable(next - current,true); m_model->setCheckable(current - next,false); m_model->commitOrder(uuids);
}
void ModListPanel::commitOrder(const QStringList& uuids) { m_model->commitOrder(uuids); m_ctx.setActive(m_model->activeUuids()); emit orderChanged(); }
void ModListPanel::moveRow(int source, int target) { m_proxy->moveRow(source,target,true); }
void ModListPanel::dragEnded() { m_ctx.setActive(m_model->activeUuids()); emit orderChanged(); }
void ModListPanel::selectionChanged(const QVariantList& values) {
    QStringList uuids;
    for (const auto& value : values) if (!value.toString().isEmpty()) uuids.append(value.toString());
    emit selectionUpdated(uuids);
}
bool ModListPanel::eventFilter(QObject* object, QEvent* event) {
    if (object == m_qml && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
        m_qml->rootContext()->setContextProperty(QStringLiteral("modListHasFocus"),event->type() == QEvent::FocusIn);
    return QWidget::eventFilter(object,event);
}
}
