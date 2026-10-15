#include "ui/plugins/organizer/view.h"

#include <algorithm>
#include <QtConcurrent/QtConcurrentRun>
#include <QCoro/QCoroFuture>
#include <QCursor>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include "core/downloads/manager.h"
#include "core/mod_service.h"
#include "core/services/diagnostics_service.h"
#include "ui/components/dialog_chrome.h"
#include "ui/components/dialogs.h"
#include "ui/components/filter_sidebar.h"
#include "ui/components/mod_activation.h"
#include "ui/components/mod_updates.h"
#include "ui/mod_selection.h"
#include "ui/panels/mod_info_panel.h"
#include "ui/plugins/organizer/dialogs.h"
#include "ui/plugins/organizer/filter_model.h"
#include "ui/plugins/organizer/tree_model.h"
#include "ui/theme/constants.h"
#include "ui/theme/palette.h"

namespace px::ui {
using namespace core::organizer;
namespace {
QString plural(qsizetype count, const QString& word) {
    return QStringLiteral("%1 %2%3").arg(count).arg(word, count == 1 ? QString{} : QStringLiteral("s"));
}
OrganizerService& organizer(core::CoreContext& ctx) {
    auto* service = dynamic_cast<OrganizerService*>(ctx.plugins().get(QStringLiteral("organizer")));
    Q_ASSERT(service);
    return *service;
}
QPushButton* button(const QString& text, QWidget* parent, QHBoxLayout* layout) {
    auto* result = new QPushButton(text, parent);
    result->setObjectName(QStringLiteral("appBtn"));
    result->setCursor(Qt::PointingHandCursor);
    layout->addWidget(result);
    return result;
}
}

OrganizerViewPanel::OrganizerViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : BaseViewPanel(ctx, engine, parent, app), service_(organizer(ctx)),
      model_(new ModTreeModel([&ctx](const QString& uuid) { return ctx.diagnosticsService().summary_for(uuid); }, this)),
      filters_(new OrganizerFilterModel(this)) {
    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("contentArea"));
    auto* row = new QHBoxLayout(content);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto* sidebar = new FilterSidebar(engine, filters_, content);
    sidebar->setFixedWidth(SIDEBAR_WIDTH);
    row->addWidget(sidebar);
    connect(sidebar, &FilterSidebar::activated, this, [this](int index) {
        const auto key = filters_->key_at(index);
        if (key && *key != filter_key_) { filter_key_ = *key; invalidate(); }
    });
    auto* main = new QWidget(content);
    main->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(main);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* toolbar = new QWidget(main);
    toolbar->setObjectName(QStringLiteral("searchBox"));
    toolbar->setFixedHeight(56);
    auto* tools = new QHBoxLayout(toolbar);
    tools->setContentsMargins(16, 8, 16, 8);
    search_ = new QLineEdit(toolbar);
    search_->setPlaceholderText(QStringLiteral("Search mods, package ID, author…"));
    search_->setClearButtonEnabled(true);
    tools->addWidget(search_, 1);
    auto* create = button(QStringLiteral("New folder"), toolbar, tools);
    create->setObjectName(QStringLiteral("primaryAction"));
    connect(create, &QPushButton::clicked, this, &OrganizerViewPanel::newFolder);
    auto* rules = button(QStringLiteral("Auto-folder rules"), toolbar, tools);
    rules->setObjectName(QStringLiteral("organizerRulesButton"));
    connect(rules, &QPushButton::clicked, this, &OrganizerViewPanel::openRules);
    connect(button(QStringLiteral("Expand all"), toolbar, tools), &QPushButton::clicked, this, [this] { treeCommand("expandAll"); });
    connect(button(QStringLiteral("Collapse all"), toolbar, tools), &QPushButton::clicked, this, [this] { treeCommand("collapseAll"); });
    layout->addWidget(toolbar);

    qml_ = new QQuickWidget(engine, main);
    qml_->setObjectName(QStringLiteral("organizerTree"));
    qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml_->setClearColor(paletteColor(u"ELEVATE_1"));
    qml_->rootContext()->setContextProperty(QStringLiteral("organizerPanel"), this);
    qml_->rootContext()->setContextProperty(QStringLiteral("modTreeModel"), model_);
    qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/ModTree.qml")));
    rules_dialog_ = new QDialog(this, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    rules_dialog_->setObjectName(QStringLiteral("organizerRuleEditorWindow"));
    rules_dialog_->setWindowTitle(QStringLiteral("Auto-Folder Rules"));
    rules_dialog_->setWindowModality(Qt::WindowModal);
    rules_dialog_->setMinimumSize(720, 520);
    rules_dialog_->setSizeGripEnabled(true);
    auto* rules_layout = new QVBoxLayout(rules_dialog_);
    rules_layout->setContentsMargins(0, 0, 0, 0);
    rules_qml_ = new QQuickWidget(engine, rules_dialog_);
    rules_qml_->setObjectName(QStringLiteral("organizerRuleEditorQuickWidget"));
    rules_qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    rules_qml_->setClearColor(paletteColor(u"ELEVATE_2"));
    rules_qml_->rootContext()->setContextProperty(QStringLiteral("organizerPanel"), this);
    rules_qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/RuleEditor.qml")));
    rules_layout->addWidget(rules_qml_);
    installDialogChrome(rules_dialog_);
    connect(rules_dialog_, &QDialog::finished, this, [this] {
        if (auto* root = rules_qml_->rootObject()) QMetaObject::invokeMethod(root, "closeEditor");
    });
    auto* hint = new QWidget(main);
    hint->setObjectName(QStringLiteral("organizerHint"));
    auto* hints = new QHBoxLayout(hint);
    hints->setContentsMargins(12, 5, 12, 5);
    auto* hint_text = new QLabel(QStringLiteral("Folders and tags are for browsing only. Edit load order in Mods; checkboxes enable or disable the same mods."), hint);
    hint_text->setWordWrap(true);
    hints->addWidget(hint_text, 1);
    auto* dismiss = button(QStringLiteral("×"), hint, hints);
    dismiss->setToolTip(QStringLiteral("Dismiss"));
    connect(dismiss, &QPushButton::clicked, hint, &QWidget::hide);
    layout->addWidget(hint);
    layout->addWidget(qml_, 1);
    selection_bar_ = new QWidget(main);
    auto* bar = new QHBoxLayout(selection_bar_);
    bar->setContentsMargins(12, 6, 12, 6);
    selection_label_ = new QLabel(selection_bar_);
    bar->addWidget(selection_label_);
    const QList<QPair<QString, QString>> actions{{QStringLiteral("Enable"), QStringLiteral("enable")}, {QStringLiteral("Disable"), QStringLiteral("disable")}, {QStringLiteral("Move to folder…"), QStringLiteral("move")}, {QStringLiteral("Manage tags…"), QStringLiteral("tags")}, {QStringLiteral("Create folder from selection"), QStringLiteral("create")}, {QStringLiteral("Update"), QStringLiteral("update")}};
    for (const auto& [label, action] : actions) {
        auto* item = button(label, selection_bar_, bar);
        if (action == QStringLiteral("update")) update_button_ = item;
        connect(item, &QPushButton::clicked, this, [this, action] { selectionAction(action); });
    }
    selection_bar_->hide();
    layout->addWidget(selection_bar_);
    row->addWidget(main, 3);
    info_ = new ModInfoPanel(ctx, engine, app.uiPrefs(), content);
    info_->setObjectName(QStringLiteral("modInfoPanel"));
    info_->setMinimumWidth(300);
    row->addWidget(info_, 2);
    selection_ = std::make_unique<ModSelectionPresenter>(ctx, *info_);
    connect(info_, &ModInfoPanel::modRequested, this, [this](const QString& uuid) {
        const auto index = model_->index_for_uuid(uuid);
        if (index.isValid()) selectSingle(index);
    });
    root_->addWidget(content, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("organizerStatus"));
    root_->addWidget(status_);
    menu_ = new QMenu(this);
    menu_->setToolTipsVisible(true);
    search_timer_ = new QTimer(this);
    search_timer_->setSingleShot(true);
    search_timer_->setInterval(220);
    connect(search_, &QLineEdit::textChanged, this, [this] { search_timer_->start(); });
    connect(search_timer_, &QTimer::timeout, this, [this] {
        if (search_text_ != search_->text()) { search_text_ = search_->text(); invalidate(); }
    });
    connect(model_, &QAbstractItemModel::modelReset, this, [this] { selection_anchor_ = QPersistentModelIndex{}; });
    changed_id_ = service_.changed.connect([this](const auto&) { invalidate(); Q_EMIT editorChanged(); });
    active_id_ = ctx.activeStateChanged().connect([this](const auto&) { invalidate(); });
    mods_id_ = ctx.modService().mods_changed.connect([this](const auto&) { invalidate(); });
    diagnostics_id_ = ctx.diagnosticsService().diagnostics_summary_changed.connect([this](const auto&) {
        model_->refresh_diagnostics();
        if (!selected_uuid_.isEmpty()) info_->setIssues(ctx_.diagnosticsService().issues_for(selected_uuid_));
    });
    busy_id_ = ctx.downloads().busy_changed.connect([this](bool) { syncInfo(); });
}

OrganizerViewPanel::~OrganizerViewPanel() {
    service_.changed.disconnect(changed_id_);
    ctx_.activeStateChanged().disconnect(active_id_);
    ctx_.modService().mods_changed.disconnect(mods_id_);
    ctx_.diagnosticsService().diagnostics_summary_changed.disconnect(diagnostics_id_);
    ctx_.downloads().busy_changed.disconnect(busy_id_);
}
bool OrganizerViewPanel::ready() const { return service_.ready(); }
void OrganizerViewPanel::showEvent(QShowEvent* event) {
    BaseViewPanel::showEvent(event);
    if (dirty_) rebuild();
    Q_EMIT readyChanged();
    if (auto* root = rules_qml_->rootObject()) QMetaObject::invokeMethod(root, "schedulePreview");
}
void OrganizerViewPanel::invalidate() { dirty_ = true; if (isVisible()) rebuild(); }
void OrganizerViewPanel::rebuild() {
    if (!ready()) return;
    dirty_ = false;
    filters_->update(service_.tree_filters());
    const auto query = filters_->for_key(filter_key_).query(search_text_);
    const auto tree = service_.tree(query);
    model_->set_tree(tree, ctx_.allMods(), ctx_.modService().provider_colors(), !query.is_empty(), service_.state().tags);
    const auto ungrouped = query.is_empty() ? tree.mods.size() : service_.tree().mods.size();
    status_->setText(QStringLiteral("%1 · %2 active · %3 · %4 ungrouped").arg(plural(ctx_.allMods().size(), QStringLiteral("mod"))).arg(ctx_.activeUuids().size()).arg(plural(service_.state().folders.size() - 1, QStringLiteral("folder"))).arg(ungrouped));
    syncInfo(true);
    Q_EMIT readyChanged();
}
QStringList OrganizerViewPanel::selectedPackageIds() const {
    QStringList result;
    for (const auto* node : model_->selected_nodes()) if (node->leaf && node->leaf->package_id && !result.contains(*node->leaf->package_id)) result.append(*node->leaf->package_id);
    return result;
}
QStringList OrganizerViewPanel::selectedUuids() const {
    QStringList result;
    for (const auto* node : model_->selected_nodes()) if (node->leaf) result.append(node->leaf->uuid);
    return result;
}
void OrganizerViewPanel::syncInfo(bool force) {
    const auto nodes = model_->selected_nodes();
    const auto uuids = selectedUuids();
    Q_EMIT editorChanged();
    selection_label_->setText(plural(uuids.size(), QStringLiteral("mod")) + QStringLiteral(" selected"));
    selection_bar_->setVisible(uuids.size() > 1);
    const auto state = update_state(ctx_, uuids);
    update_button_->setVisible(ctx_.downloads().available());
    update_button_->setText(state.label);
    update_button_->setEnabled(state.enabled);
    update_button_->setToolTip(state.tooltip);
    const QString uuid = nodes.size() == 1 && nodes.front()->leaf ? nodes.front()->leaf->uuid : QString{};
    if (uuid == selected_uuid_ && !force) return;
    selected_uuid_ = uuid;
    if (uuid.isEmpty()) selection_->clear(); else selection_->show(uuid);
}
int OrganizerViewPanel::selectedTagCount() const { return selectedPackageIds().size(); }
QVariantList OrganizerViewPanel::tagRows() const {
    QVariantList result;
    if (!ready()) return result;
    auto tags = service_.state().tags.values();
    std::ranges::sort(tags, [](const Tag& a, const Tag& b) { return a.name.toCaseFolded() < b.name.toCaseFolded(); });
    const auto pids = selectedPackageIds();
    for (const auto& tag : tags) {
        int matched = 0;
        for (const auto& pid : pids) if (service_.state().mod_tags.value(pid).contains(tag.id)) ++matched;
        result.append(QVariantMap{{QStringLiteral("id"), tag.id}, {QStringLiteral("name"), tag.name}, {QStringLiteral("color"), tag.color}, {QStringLiteral("checkState"), matched > 0 && matched < pids.size() ? 2 : int(matched > 0)}});
    }
    return result;
}
QVariantList OrganizerViewPanel::ruleRows() const {
    QVariantList result;
    if (!ready()) return result;
    for (const auto& rule : service_.state().rules) result.append(QVariantMap{{QStringLiteral("field"), QString(rule_field_name(rule.field))}, {QStringLiteral("op"), QString(rule_op_name(rule.op))}, {QStringLiteral("pattern"), rule.pattern}, {QStringLiteral("folder_id"), rule.folder_id}});
    return result;
}
QVariantList OrganizerViewPanel::ruleFolders() const {
    QVariantList result;
    if (!ready()) return result;
    const auto& folders = service_.state().folders;
    QList<QPair<QString, qint64>> paths;
    paths.reserve(folders.size() - 1);
    for (const auto& folder : folders) {
        if (folder.id == ROOT_ID) continue;
        QStringList names;
        qint64 id = folder.id;
        while (id != ROOT_ID) {
            const auto& ancestor = *folders.constFind(id);
            names.prepend(ancestor.name);
            id = *ancestor.parent_id;
        }
        paths.append({names.join(QStringLiteral(" / ")), folder.id});
    }
    std::ranges::sort(paths, [](const auto& a, const auto& b) {
        return QString::compare(a.first, b.first, Qt::CaseInsensitive) < 0;
    });
    result.reserve(paths.size());
    for (const auto& [name, id] : paths)
        result.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}});
    return result;
}
core::Result<QList<RuleSpec>> OrganizerViewPanel::ruleSpecs(const QVariantList& rows) const {
    QList<RuleSpec> specs;
    specs.reserve(rows.size());
    for (const auto& value : rows) {
        const auto row = value.toMap();
        const auto field = parse_rule_field(row.value(QStringLiteral("field")).toString());
        const auto op = parse_rule_op(row.value(QStringLiteral("op")).toString());
        if (!field || !op) return std::unexpected(core::Error{QStringLiteral("Invalid rule field or operator")});
        specs.append(RuleSpec{*field, *op, row.value(QStringLiteral("pattern")).toString(), row.value(QStringLiteral("folder_id")).toLongLong()});
    }
    return specs;
}
QCoro::QmlTask OrganizerViewPanel::requestRulePreview(const QVariantList& rows, int revision) { return calculateRulePreview(rows, revision); }
QCoro::Task<void> OrganizerViewPanel::calculateRulePreview(QVariantList rows, int revision) {
    if (!ready()) co_return;
    const auto specs = ruleSpecs(rows);
    if (!specs) co_return;
    const QPointer<OrganizerViewPanel> alive(this);
    auto future = QtConcurrent::run([state = service_.state(), mods = ctx_.allMods(), specs = *specs] { return preview_rule_matches(state, mods, specs); });
    const auto preview = co_await future;
    if (!alive) co_return;
    QVariantList counts;
    counts.reserve(preview.counts.size());
    for (int count : preview.counts) counts.append(count);
    QVariantMap result;
    result.insert(QStringLiteral("counts"), counts);
    result.insert(QStringLiteral("assignable"), preview.assignable);
    Q_EMIT rulePreviewReady(revision, result);
}
void OrganizerViewPanel::setEditorError(QString message) {
    if (message != editor_error_) { editor_error_ = std::move(message); Q_EMIT editorChanged(); }
}
void OrganizerViewPanel::reportError(const core::Error& error) {
    if (auto* toasts = app_.toasts()) toasts->error(error.message);
    else spdlog::warn("organizer: {}", error.message.toStdString());
}
void OrganizerViewPanel::treeCommand(const char* method) { if (auto* root = qml_->rootObject()) QMetaObject::invokeMethod(root, method); }
void OrganizerViewPanel::openTags() { setEditorError(); treeCommand("openTags"); }
void OrganizerViewPanel::openRules() {
    setEditorError();
    auto* root = rules_qml_->rootObject();
    if (!root) return;
    auto* host = window();
    if (!rules_opened_once_) {
        const auto available = host->screen()->availableGeometry();
        rules_dialog_->resize(std::min({960, std::max(800, host->width() - 48), available.width() - 32}), std::min({760, std::max(600, host->height() - 80), available.height() - 32}));
        rules_opened_once_ = true;
    }
    rules_dialog_->move(host->frameGeometry().center() - rules_dialog_->rect().center());
    rules_dialog_->open();
    rules_dialog_->activateWindow();
    rules_qml_->setFocus(Qt::OtherFocusReason);
    QMetaObject::invokeMethod(root, "prepareForOpen");
}
void OrganizerViewPanel::closeRules() { rules_dialog_->close(); }
QCoro::QmlTask OrganizerViewPanel::createTag(QString name, QString color) { return mutateTag(QStringLiteral("create"), 0, std::move(name), std::move(color)); }
QCoro::QmlTask OrganizerViewPanel::updateTag(qint64 id, QString name, QString color) { return mutateTag(QStringLiteral("update"), id, std::move(name), std::move(color)); }
QCoro::QmlTask OrganizerViewPanel::deleteTag(qint64 id) { return mutateTag(QStringLiteral("delete"), id); }
QCoro::QmlTask OrganizerViewPanel::assignTag(qint64 id, bool add) { return mutateTag(QStringLiteral("assign"), id, {}, {}, add); }
QCoro::Task<void> OrganizerViewPanel::mutateTag(QString operation, qint64 id, QString name, QString color, bool add) {
    const QPointer<OrganizerViewPanel> alive(this);
    if (operation == QStringLiteral("create")) {
        const auto result = co_await service_.create_tag(std::move(name), std::move(color));
        if (!alive) co_return;
        if (!result) { setEditorError(result.error().message); co_return; }
        Q_EMIT tagCreated();
    } else {
        core::Result<void> result;
        if (operation == QStringLiteral("update")) result = co_await service_.update_tag(id, std::move(name), std::move(color));
        else if (operation == QStringLiteral("delete")) result = co_await service_.delete_tag(id);
        else result = co_await service_.set_mod_tags(selectedPackageIds(), add ? QSet<qint64>{id} : QSet<qint64>{}, add ? QSet<qint64>{} : QSet<qint64>{id});
        if (!alive) co_return;
        if (!result) { setEditorError(result.error().message); co_return; }
        if (operation == QStringLiteral("update")) Q_EMIT tagUpdated(id);
    }
    setEditorError();
}
QCoro::QmlTask OrganizerViewPanel::saveRules(QVariantList rows) { return persistRules(std::move(rows)); }
QCoro::Task<void> OrganizerViewPanel::persistRules(QVariantList rows) {
    const QPointer<OrganizerViewPanel> alive(this);
    auto specs = ruleSpecs(rows);
    if (!specs) { setEditorError(specs.error().message); co_return; }
    const auto result = co_await service_.set_rules(std::move(*specs));
    if (!alive) co_return;
    if (!result) { setEditorError(result.error().message); co_return; }
    setEditorError();
    Q_EMIT rulesSaved();
}
QCoro::QmlTask OrganizerViewPanel::addStandardRules() { return standardRules(); }
QCoro::Task<void> OrganizerViewPanel::standardRules() {
    const QPointer<OrganizerViewPanel> alive(this);
    const auto result = co_await service_.add_standard_rules();
    if (!alive) co_return;
    setEditorError(result ? QString{} : result.error().message);
}
QVariantList OrganizerViewPanel::expandedIndexes() const {
    QVariantList result;
    for (const auto& index : model_->expanded_indexes()) result.append(QVariant::fromValue(index));
    return result;
}
void OrganizerViewPanel::selectRow(const QModelIndex& index, int modifiers, const QVariantList& range) {
    if ((modifiers & Qt::ShiftModifier) && selection_anchor_.isValid()) {
        QList<QModelIndex> indexes;
        for (const auto& value : range) indexes.append(value.value<QModelIndex>());
        model_->select(indexes);
    } else {
        model_->select({index}, modifiers & Qt::ControlModifier ? QStringLiteral("toggle") : QStringLiteral("replace"));
        selection_anchor_ = index;
    }
    syncInfo();
}
void OrganizerViewPanel::selectSingle(const QModelIndex& index) {
    if (!model_->is_selected(index)) model_->select({index});
    selection_anchor_ = index;
    syncInfo();
}
QCoro::QmlTask OrganizerViewPanel::persistAllCollapsed(bool collapsed) { return persistAllCollapse(collapsed); }
QCoro::Task<void> OrganizerViewPanel::persistAllCollapse(bool collapsed) {
    const QPointer<OrganizerViewPanel> alive(this);
    const auto result = co_await service_.set_all_collapsed(collapsed);
    if (!alive) co_return;
    if (!result) reportError(result.error());
}
QCoro::QmlTask OrganizerViewPanel::persistCollapsed(const QModelIndex& index, bool collapsed) { return persistCollapse(index, collapsed); }
QCoro::Task<void> OrganizerViewPanel::persistCollapse(QModelIndex index, bool collapsed) {
    const auto* node = model_->node_at(index);
    if (!node || !node->is_container() || model_->filtering()) co_return;
    const QPointer<OrganizerViewPanel> alive(this);
    const auto result = co_await service_.set_collapsed(node->folder_id(), collapsed);
    if (!alive) co_return;
    if (!result) reportError(result.error());
}
QCoro::QmlTask OrganizerViewPanel::toggleCheck(const QModelIndex& index) { return toggle(index); }
QCoro::Task<void> OrganizerViewPanel::toggle(QModelIndex index) {
    const auto* node = model_->node_at(index);
    if (!node) co_return;
    if (node->leaf) co_await toggle_mods(ctx_, this, {node->leaf->uuid});
    else if (node->is_container()) {
        const auto change = service_.folder_toggle(node->folder_id(), node->kind != QStringLiteral("ungrouped"));
        if (!change) { reportError(change.error()); co_return; }
        if (!change->enable.isEmpty() || !change->disable.isEmpty()) co_await apply_activation(ctx_, this, change->enable, change->disable);
    }
}
QCoro::QmlTask OrganizerViewPanel::selectionAction(const QString& action) { return runSelectionAction(action); }
QCoro::Task<void> OrganizerViewPanel::runSelectionAction(QString action) {
    const auto uuids = selectedUuids();
    if (uuids.isEmpty()) co_return;
    if (action == QStringLiteral("tags")) openTags();
    else if (action == QStringLiteral("move") || action == QStringLiteral("create")) {
        const auto pids = selectedPackageIds();
        if (!pids.isEmpty()) co_await editFolder(action, ROOT_ID, pids);
    } else if (action == QStringLiteral("update")) co_await update_mods(ctx_, uuids, this);
    else if (action == QStringLiteral("enable")) co_await apply_activation(ctx_, this, uuids);
    else if (action == QStringLiteral("disable")) co_await apply_activation(ctx_, this, {}, uuids);
}
QCoro::QmlTask OrganizerViewPanel::newFolder() { return editFolder(QStringLiteral("new")); }
QCoro::Task<void> OrganizerViewPanel::editFolder(QString action, qint64 id, QStringList pids) {
    const QPointer<OrganizerViewPanel> alive(this);
    core::Result<void> result;
    if (action == QStringLiteral("new") || action == QStringLiteral("create") || action == QStringLiteral("rename")) {
        const bool rename = action == QStringLiteral("rename");
        const QString initial = rename ? service_.state().folders.value(id).name : QString{};
        QString title;
        if (rename) title = QStringLiteral("Rename Folder");
        else if (action == QStringLiteral("create")) title = QStringLiteral("Create Folder from Selection");
        else title = QStringLiteral("New Folder");
        const auto nameResult = co_await awaitDialog<FolderNameDialog>(title, initial, this, rename ? QStringLiteral("Rename") : QStringLiteral("Create"));
        const int code = nameResult.first;
        auto* const dialog = nameResult.second;
        if (!alive) co_return;
        if (code != QDialog::Accepted) co_return;
        const auto name = dialog->textValue();
        if (rename) result = co_await service_.rename_folder(id, name);
        else {
            core::Result<core::organizer::Folder> created;
            if (action == QStringLiteral("create")) created = co_await service_.create_folder_from(name, pids);
            else created = co_await service_.create_folder(name, id);
            if (!alive) co_return;
            if (!created) reportError(created.error());
            co_return;
        }
    } else if (action == QStringLiteral("move") || action == QStringLiteral("moveFolder")) {
        QSet<qint64> allowed;
        if (action == QStringLiteral("moveFolder")) {
            const auto targets = service_.folder_move_targets(id);
            if (!targets) { reportError(targets.error()); co_return; }
            allowed = *targets;
        } else {
            for (auto it = service_.state().folders.cbegin(); it != service_.state().folders.cend(); ++it) allowed.insert(it.key());
        }
        const auto pickerResult = co_await awaitDialog<FolderPickerDialog>(service_.tree(), action == QStringLiteral("move") ? QStringLiteral("Move to Folder") : QStringLiteral("Move Folder"), allowed, this);
        const int code = pickerResult.first;
        auto* const dialog = pickerResult.second;
        if (!alive) co_return;
        if (code != QDialog::Accepted) co_return;
        const auto destination = dialog->folder_id();
        if (action == QStringLiteral("moveFolder")) result = co_await service_.move_folder(id, destination);
        else { co_await placeMods(pids, destination); co_return; }
    } else if (action == QStringLiteral("delete")) {
        const auto deleteResult = co_await awaitDialog<DeleteFolderDialog>(service_.state().folders.value(id).name, this);
        const int code = deleteResult.first;
        if (!alive) co_return;
        if (code != QMessageBox::Yes) co_return;
        result = co_await service_.delete_folder(id);
    }
    if (!alive) co_return;
    if (!result) reportError(result.error());
}
QCoro::Task<void> OrganizerViewPanel::placeMods(QStringList pids, qint64 destination) {
    const QPointer<OrganizerViewPanel> alive(this);
    core::Result<void> result;
    if (destination == ROOT_ID) result = co_await service_.ungroup(pids);
    else result = co_await service_.place(pids, destination);
    if (!alive) co_return;
    if (!result) reportError(result.error());
}
QCoro::Task<void> OrganizerViewPanel::resetRules(QStringList pids) {
    const QPointer<OrganizerViewPanel> alive(this);
    const auto result = co_await service_.reset_to_rules(pids);
    if (!alive) co_return;
    if (!result) reportError(result.error());
}
void OrganizerViewPanel::contextMenu(const QModelIndex& index) {
    const auto* node = model_->node_at(index);
    menu_->clear();
    if (!node) menu_->addAction(QStringLiteral("New folder…"), this, &OrganizerViewPanel::newFolder);
    else if (node->leaf) { selectSingle(index); modMenu(); }
    else { model_->select({index}); folderMenu(*node); }
    syncInfo();
    menu_->popup(QCursor::pos());
}
void OrganizerViewPanel::modMenu() {
    const auto nodes = model_->selected_nodes();
    const auto uuids = selectedUuids();
    bool inactive = false, active = false;
    for (const auto& uuid : uuids) { if (ctx_.activeUuids().contains(uuid)) active = true; else inactive = true; }
    if (inactive) menu_->addAction(QStringLiteral("Enable"), this, [this, uuids] { apply_activation(ctx_, this, uuids); });
    if (active) menu_->addAction(QStringLiteral("Disable"), this, [this, uuids] { apply_activation(ctx_, this, {}, uuids); });
    add_update_action(menu_, ctx_, uuids, [this, uuids] { update_mods(ctx_, uuids, this); });
    menu_->addSeparator();
    const auto pids = selectedPackageIds();
    bool placeable = !pids.isEmpty(), grouped = false;
    QStringList manual;
    for (const auto* node : nodes) if (node->leaf) {
        placeable &= node->leaf->package_id.has_value();
        grouped |= node->folder_id() != ROOT_ID;
        if (node->leaf->package_id && node->leaf->placement == Placement::Manual && !manual.contains(*node->leaf->package_id)) manual.append(*node->leaf->package_id);
    }
    auto* move = menu_->addAction(QStringLiteral("Move to folder…"), this, [this, pids] { editFolder(QStringLiteral("move"), ROOT_ID, pids); });
    auto* remove = menu_->addAction(QStringLiteral("Remove from folder"), this, [this, pids] { placeMods(pids, ROOT_ID); });
    auto* create = menu_->addAction(QStringLiteral("Create folder from selection…"), this, [this, pids] { editFolder(QStringLiteral("create"), ROOT_ID, pids); });
    for (auto* action : {move, remove, create}) { action->setEnabled(placeable); action->setToolTip(placeable ? QString{} : QStringLiteral("Mods without a package ID cannot be placed in folders")); }
    remove->setEnabled(placeable && grouped);
    menu_->addAction(QStringLiteral("Manage tags…"), this, &OrganizerViewPanel::openTags)->setEnabled(placeable);
    if (placeable && !manual.isEmpty()) menu_->addAction(QStringLiteral("Use auto rules"), this, [this, manual] { resetRules(manual); });
}
void OrganizerViewPanel::folderMenu(const TreeNode& node) {
    const auto id = node.folder_id();
    const auto result = service_.folder_mod_uuids(id, node.kind != QStringLiteral("ungrouped"));
    if (!result) { reportError(result.error()); return; }
    const auto uuids = *result;
    menu_->addAction(QStringLiteral("Enable all"), this, [this, uuids] { apply_activation(ctx_, this, uuids); })->setEnabled(!uuids.isEmpty());
    menu_->addAction(QStringLiteral("Disable all"), this, [this, uuids] { apply_activation(ctx_, this, {}, uuids); })->setEnabled(!uuids.isEmpty());
    add_update_action(menu_, ctx_, uuids, [this, uuids] { update_mods(ctx_, uuids, this); });
    menu_->addSeparator();
    if (node.kind == QStringLiteral("ungrouped")) { menu_->addAction(QStringLiteral("New folder…"), this, &OrganizerViewPanel::newFolder); return; }
    if (node.folder && node.folder->depth < MAX_DEPTH) menu_->addAction(QStringLiteral("New subfolder…"), this, [this, id] { editFolder(QStringLiteral("new"), id); });
    menu_->addAction(QStringLiteral("Rename…"), this, [this, id] { editFolder(QStringLiteral("rename"), id); });
    const auto targets = service_.folder_move_targets(id);
    menu_->addAction(QStringLiteral("Move to…"), this, [this, id] { editFolder(QStringLiteral("moveFolder"), id); })->setEnabled(targets && !targets->isEmpty());
    menu_->addAction(QStringLiteral("Delete…"), this, [this, id] { editFolder(QStringLiteral("delete"), id); });
}
bool OrganizerViewPanel::canDrop(const QModelIndex& source, const QModelIndex& target) const {
    const auto* src = model_->node_at(source);
    const auto* dst = model_->node_at(target);
    if (!src || !dst || !dst->is_container() || src == dst) return false;
    if (src->leaf) {
        if (!model_->is_selected(source)) return src->leaf->package_id.has_value();
        bool any = false;
        for (const auto* node : model_->selected_nodes()) {
            if (!node->leaf) continue;
            any = true;
            if (!node->leaf->package_id) return false;
        }
        return any;
    }
    if (src->kind == QStringLiteral("folder")) { const auto targets = service_.folder_move_targets(src->folder_id()); return targets && targets->contains(dst->folder_id()); }
    return false;
}
QCoro::QmlTask OrganizerViewPanel::dropOn(const QModelIndex& source, const QModelIndex& target) { return performDrop(source, target); }
QCoro::Task<void> OrganizerViewPanel::performDrop(QModelIndex source, QModelIndex target) {
    const QPointer<OrganizerViewPanel> alive(this);
    if (!canDrop(source, target)) co_return;
    const auto* src = model_->node_at(source);
    const auto* dst = model_->node_at(target);
    const auto destination = dst->folder_id();
    if (src->kind == QStringLiteral("folder")) {
        const auto result = co_await service_.move_folder(src->folder_id(), destination);
        if (!alive) co_return;
        if (!result) reportError(result.error());
    } else {
        const auto pids = model_->is_selected(source) ? selectedPackageIds() : QStringList{*src->leaf->package_id};
        co_await placeMods(pids, destination);
    }
}
}
