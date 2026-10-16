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
    : QWidget(parent), ctx_(ctx), model_(new ModListModel({}, &ctx.diagnosticsService(), this)),
      proxy_(new ModListProxyModel(model_, this)), qml_(new QQuickWidget(engine, this)),
      compact_mode_(ctx.config().compact_mod_list), highlight_timer_(new QTimer(this)) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto* search = new QWidget(this); search->setObjectName(QStringLiteral("searchBox")); search->setFixedHeight(56);
    auto* search_layout = new QHBoxLayout(search); search_layout->setContentsMargins(16,8,16,8);
    search_input = new QLineEdit(search);
    search_input->setObjectName(QStringLiteral("searchInput"));
    search_input->setPlaceholderText(QStringLiteral("Search mods, package ID, author…"));
    search_input->setClearButtonEnabled(true);
    connect(search_input, &QLineEdit::textChanged, this, &ModListPanel::set_search_filter);
    if (const auto search_icon = icon(u"search",16,paletteValue(u"TEXT_DIM"))) search_input->addAction(*search_icon,QLineEdit::LeadingPosition);
    search_layout->addWidget(search_input); layout->addWidget(search);
    connect(model_, &ModListModel::active_mods_changed, this, &ModListPanel::active_mods_changed);
    qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml_->setAttribute(Qt::WA_AlwaysStackOnTop,false);
    qml_->setClearColor(paletteColor(u"ELEVATE_1")); qml_->setAcceptDrops(true);
    qml_->rootContext()->setContextProperty(QStringLiteral("modListPanel"),this);
    qml_->rootContext()->setContextProperty(QStringLiteral("modListModel"),proxy_);
    qml_->rootContext()->setContextProperty(QStringLiteral("modListHasFocus"),false);
    qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/ModList.qml")));
    layout->addWidget(qml_);
    qml_->setFocusPolicy(Qt::StrongFocus); qml_->installEventFilter(this);
    highlight_timer_->setSingleShot(true); highlight_timer_->setInterval(1200);
    connect(highlight_timer_, &QTimer::timeout, this, [this] { set_highlighted({}); });
    active_connection_ = ctx_.activeStateChanged().connect([this](const auto& active) { core_state_changed(active); });
    config_connection_ = ctx_.configChanged().connect([this](const auto& cfg) { setCompactMode(cfg.compact_mod_list); });
    mods_connection_ = ctx_.modService().mods_changed.connect([this](const auto&) { refresh(); });
}
ModListPanel::~ModListPanel() {
    ctx_.activeStateChanged().disconnect(active_connection_);
    ctx_.configChanged().disconnect(config_connection_);
    ctx_.modService().mods_changed.disconnect(mods_connection_);
    // QML bindings refer to the models and panel during destruction.
    delete qml_;
}
void ModListPanel::load_mods(const QHash<QString, ListedModPtr>& mods, const QStringList& active) { model_->load_mods(mods,active); }
QStringList ModListPanel::active_uuids() const { return model_->active_uuids(); }
void ModListPanel::set_sidebar_filter(std::optional<QSet<QString>> uuids) { proxy_->set_sidebar_filter(std::move(uuids)); }
void ModListPanel::set_search_filter(const QString& text) { proxy_->set_search_filter(text); }
void ModListPanel::setCompactMode(bool enabled) {
    if (compact_mode_ == enabled) return;
    compact_mode_ = enabled; emit compact_mode_changed(enabled);
}
bool ModListPanel::isHighlighted(const QString& uuid) const { return highlighted_.contains(uuid); }
void ModListPanel::modSelected(const QString& uuid) { emit mod_selected(uuid); }
int ModListPanel::proxy_to_source_row(int row) const { return proxy_->mapToSource(proxy_->index(row)).row(); }
QString ModListPanel::uuidAt(int row) const { const auto* item = model_->get_item(proxy_to_source_row(row)); return item ? item->uuid : QString{}; }
int ModListPanel::rowForUuid(const QString& uuid) const { return proxy_->proxy_row_for_uuid(uuid); }
bool ModListPanel::isChecked(int row) const { const auto* item = model_->get_item(proxy_to_source_row(row)); return item && item->checked; }
bool ModListPanel::select_uuid(const QString& uuid) {
    const int row = rowForUuid(uuid);
    auto* root = qml_->rootObject();
    if (row < 0 || !root) return false;
    return QMetaObject::invokeMethod(root,"revealRow",Qt::DirectConnection,Q_ARG(QVariant,QVariant(row)),Q_ARG(QVariant,QVariant(uuid)));
}
void ModListPanel::clearSelection() {
    auto* root = qml_->rootObject();
    auto* view = root ? root->findChild<QObject*>(QStringLiteral("listView")) : nullptr;
    if (view) {
        view->setProperty("currentIndex",-1); view->setProperty("anchorIndex",-1);
        view->setProperty("selectedIndices",QVariantList{}); view->setProperty("selectedUuids",QVariantList{});
        view->setProperty("currentUuid",QString{}); view->setProperty("anchorUuid",QString{});
    }
    emit selection_changed({}); emit mod_selected({});
}
QCoro::Task<void> ModListPanel::refresh() {
    model_->update_provider_colors(ctx_.modService().provider_colors());
    model_->load_mods(ctx_.allMods(),ctx_.activeUuids());
    QPointer<ModListPanel> self(this);
    const auto impacts = co_await ctx_.modService().startup_impact().get_all_averages();
    if (self && impacts) self->model_->set_startup_impact(*impacts);
}
QCoro::QmlTask ModListPanel::toggleChecked(const QVariantList& rows) {
    QList<int> selected; selected.reserve(rows.size());
    for (const auto& row : rows) selected.append(row.toInt());
    return toggle_rows(std::move(selected));
}
QCoro::Task<void> ModListPanel::toggle_rows(QList<int> rows) {
    QStringList selected;
    for (const int row : rows) { const auto uuid = uuidAt(row); if (!uuid.isEmpty()) selected.append(uuid); }
    if (selected.isEmpty()) co_return;
    const QSet<QString> previous(ctx_.activeUuids().begin(),ctx_.activeUuids().end());
    QPointer<ModListPanel> self(this);
    co_await toggle_mods(ctx_,this,selected);
    if (!self) co_return;
    QStringList newly_enabled;
    for (const auto& uuid : selected) if (!previous.contains(uuid) && ctx_.activeUuids().contains(uuid)) newly_enabled.append(uuid);
    if (!newly_enabled.isEmpty()) highlight_rows(newly_enabled);
}
void ModListPanel::set_highlighted(const QStringList& uuids) {
    highlighted_ = QSet<QString>(uuids.begin(),uuids.end()); ++highlight_revision_; emit highlighted_changed();
}
void ModListPanel::highlight_rows(const QStringList& uuids) {
    int last = -1;
    for (const auto& uuid : uuids) if (const int row = rowForUuid(uuid); row >= 0) last = row;
    auto* root = qml_->rootObject();
    if (last < 0 || !root) return;
    if (auto* view = root->findChild<QObject*>(QStringLiteral("listView")))
        QMetaObject::invokeMethod(view,"positionViewAtIndex",Qt::DirectConnection,Q_ARG(int,last),Q_ARG(int,4));
    set_highlighted(uuids); highlight_timer_->start();
}
void ModListPanel::showContextMenu(const QVariantList& values) {
    if (!ctx_.downloads().available()) return;
    QStringList uuids; uuids.reserve(values.size());
    for (const auto& value : values) uuids.append(value.toString());
    auto* menu = new QMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose); menu->setToolTipsVisible(true);
    add_update_action(menu,ctx_,uuids,[this,uuids] { update_mods(ctx_,uuids,this); });
    menu->popup(QCursor::pos());
}
void ModListPanel::core_state_changed(const QStringList& uuids) {
    const auto current_list = model_->active_uuids();
    const QSet<QString> current(current_list.begin(),current_list.end()), next(uuids.begin(),uuids.end());
    model_->set_checkable(next - current,true); model_->set_checkable(current - next,false); model_->commitOrder(uuids);
}
void ModListPanel::commitOrder(const QStringList& uuids) { model_->commitOrder(uuids); ctx_.setActive(model_->active_uuids()); emit order_changed(); }
void ModListPanel::moveRow(int source, int target) { proxy_->move_row(source,target,true); }
void ModListPanel::dragEnded() { ctx_.setActive(model_->active_uuids()); emit order_changed(); }
void ModListPanel::selectionChanged(const QVariantList& values) {
    QStringList uuids;
    for (const auto& value : values) if (!value.toString().isEmpty()) uuids.append(value.toString());
    emit selection_changed(uuids);
}
bool ModListPanel::eventFilter(QObject* object, QEvent* event) {
    if (object == qml_ && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
        qml_->rootContext()->setContextProperty(QStringLiteral("modListHasFocus"),event->type() == QEvent::FocusIn);
    return QWidget::eventFilter(object,event);
}
}
