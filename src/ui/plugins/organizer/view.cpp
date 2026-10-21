#include "ui/plugins/organizer/view.hpp"

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
#include <algorithm>

#include <QCoro/QCoroFuture>
#include <QtConcurrent/QtConcurrentRun>

#include "core/downloads/manager.hpp"
#include "core/mod_service.hpp"
#include "core/services/diagnostics_service.hpp"
#include "ui/components/dialog_chrome.hpp"
#include "ui/components/dialogs.hpp"
#include "ui/components/filter_sidebar.hpp"
#include "ui/components/mod_activation.hpp"
#include "ui/components/mod_updates.hpp"
#include "ui/mod_selection.hpp"
#include "ui/panels/mod_info_panel.hpp"
#include "ui/plugins/organizer/dialogs.hpp"
#include "ui/plugins/organizer/filter_model.hpp"
#include "ui/plugins/organizer/tree_model.hpp"
#include "ui/theme/constants.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {
using namespace organizer;
namespace {
QString plural(qsizetype count, const QString& word) {
  return QStringLiteral("%1 %2%3").arg(count).arg(
      word, count == 1 ? QString{} : QStringLiteral("s"));
}
OrganizerService& organizerService(CoreContext& ctx) {
  auto* service = dynamic_cast<OrganizerService*>(
      ctx.plugins().get(QStringLiteral("organizer")));
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
}  // namespace

OrganizerViewPanel::OrganizerViewPanel(CoreContext& ctx, QQmlEngine* engine,
                                       QWidget* parent, AppContext& app)
    : BaseViewPanel(ctx, engine, parent, app),
      m_service(organizerService(ctx)),
      m_model(new ModTreeModel(
          [&ctx](const QString& uuid) {
            return ctx.diagnosticsService().summaryFor(uuid);
          },
          this)),
      m_filters(new OrganizerFilterModel(this)) {
  auto* content = new QWidget(this);
  content->setObjectName(QStringLiteral("contentArea"));
  auto* row = new QHBoxLayout(content);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(0);
  auto* sidebar = new FilterSidebar(engine, m_filters, content);
  sidebar->setFixedWidth(kSidebarWidth);
  row->addWidget(sidebar);
  connect(sidebar, &FilterSidebar::activated, this, [this](int index) {
    const auto key = m_filters->keyAt(index);
    if (key && *key != m_filterKey) {
      m_filterKey = *key;
      invalidate();
    }
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
  m_search = new QLineEdit(toolbar);
  m_search->setPlaceholderText(
      QStringLiteral("Search mods, package ID, author…"));
  m_search->setClearButtonEnabled(true);
  tools->addWidget(m_search, 1);
  auto* create = button(QStringLiteral("New folder"), toolbar, tools);
  create->setObjectName(QStringLiteral("primaryAction"));
  connect(create, &QPushButton::clicked, this, &OrganizerViewPanel::newFolder);
  auto* rules = button(QStringLiteral("Auto-folder rules"), toolbar, tools);
  rules->setObjectName(QStringLiteral("organizerRulesButton"));
  connect(rules, &QPushButton::clicked, this, &OrganizerViewPanel::openRules);
  connect(button(QStringLiteral("Expand all"), toolbar, tools),
          &QPushButton::clicked, this, [this] { treeCommand("expandAll"); });
  connect(button(QStringLiteral("Collapse all"), toolbar, tools),
          &QPushButton::clicked, this, [this] { treeCommand("collapseAll"); });
  layout->addWidget(toolbar);

  m_qml = new QQuickWidget(engine, main);
  m_qml->setObjectName(QStringLiteral("organizerTree"));
  m_qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
  m_qml->setClearColor(paletteColor(u"ELEVATE_1"));
  m_qml->rootContext()->setContextProperty(QStringLiteral("organizerPanel"),
                                           this);
  m_qml->rootContext()->setContextProperty(QStringLiteral("modTreeModel"),
                                           m_model);
  m_qml->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/ModTree.qml")));
  m_rulesDialog = new QDialog(
      this, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
  m_rulesDialog->setObjectName(QStringLiteral("organizerRuleEditorWindow"));
  m_rulesDialog->setWindowTitle(QStringLiteral("Auto-Folder Rules"));
  m_rulesDialog->setWindowModality(Qt::WindowModal);
  m_rulesDialog->setMinimumSize(720, 520);
  m_rulesDialog->setSizeGripEnabled(true);
  auto* rulesLayout = new QVBoxLayout(m_rulesDialog);
  rulesLayout->setContentsMargins(0, 0, 0, 0);
  m_rulesQml = new QQuickWidget(engine, m_rulesDialog);
  m_rulesQml->setObjectName(QStringLiteral("organizerRuleEditorQuickWidget"));
  m_rulesQml->setResizeMode(QQuickWidget::SizeRootObjectToView);
  m_rulesQml->setClearColor(paletteColor(u"ELEVATE_2"));
  m_rulesQml->rootContext()->setContextProperty(
      QStringLiteral("organizerPanel"), this);
  m_rulesQml->setSource(
      QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/RuleEditor.qml")));
  rulesLayout->addWidget(m_rulesQml);
  installDialogChrome(m_rulesDialog);
  connect(m_rulesDialog, &QDialog::finished, this, [this] {
    if (auto* root = m_rulesQml->rootObject())
      QMetaObject::invokeMethod(root, "closeEditor");
  });
  auto* hint = new QWidget(main);
  hint->setObjectName(QStringLiteral("organizerHint"));
  auto* hints = new QHBoxLayout(hint);
  hints->setContentsMargins(12, 5, 12, 5);
  auto* hintText = new QLabel(
      QStringLiteral("Folders and tags are for browsing only. Edit load order "
                     "in Mods; checkboxes enable or disable the same mods."),
      hint);
  hintText->setWordWrap(true);
  hints->addWidget(hintText, 1);
  auto* dismiss = button(QStringLiteral("×"), hint, hints);
  dismiss->setToolTip(QStringLiteral("Dismiss"));
  connect(dismiss, &QPushButton::clicked, hint, &QWidget::hide);
  layout->addWidget(hint);
  layout->addWidget(m_qml, 1);
  m_selectionBar = new QWidget(main);
  auto* bar = new QHBoxLayout(m_selectionBar);
  bar->setContentsMargins(12, 6, 12, 6);
  m_selectionLabel = new QLabel(m_selectionBar);
  bar->addWidget(m_selectionLabel);
  const QList<QPair<QString, QString>> actions{
      {QStringLiteral("Enable"), QStringLiteral("enable")},
      {QStringLiteral("Disable"), QStringLiteral("disable")},
      {QStringLiteral("Move to folder…"), QStringLiteral("move")},
      {QStringLiteral("Manage tags…"), QStringLiteral("tags")},
      {QStringLiteral("Create folder from selection"),
       QStringLiteral("create")},
      {QStringLiteral("Update"), QStringLiteral("update")}};
  for (const auto& [label, action] : actions) {
    auto* item = button(label, m_selectionBar, bar);
    if (action == QStringLiteral("update")) m_updateButton = item;
    connect(item, &QPushButton::clicked, this,
            [this, action] { runSelectionAction(action); });
  }
  m_selectionBar->hide();
  layout->addWidget(m_selectionBar);
  row->addWidget(main, 3);
  m_info = new ModInfoPanel(ctx, engine, app.uiPrefs(), content);
  m_info->setObjectName(QStringLiteral("modInfoPanel"));
  m_info->setMinimumWidth(300);
  row->addWidget(m_info, 2);
  m_selection = std::make_unique<ModSelectionPresenter>(ctx, *m_info);
  connect(m_info, &ModInfoPanel::modRequested, this,
          [this](const QString& uuid) {
            const auto index = m_model->indexForUuid(uuid);
            if (index.isValid()) selectSingle(index);
          });
  m_root->addWidget(content, 1);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("organizerStatus"));
  m_root->addWidget(m_status);
  m_menu = new QMenu(this);
  m_menu->setToolTipsVisible(true);
  m_searchTimer = new QTimer(this);
  m_searchTimer->setSingleShot(true);
  m_searchTimer->setInterval(220);
  connect(m_search, &QLineEdit::textChanged, this,
          [this] { m_searchTimer->start(); });
  connect(m_searchTimer, &QTimer::timeout, this, [this] {
    if (m_searchText != m_search->text()) {
      m_searchText = m_search->text();
      invalidate();
    }
  });
  connect(m_model, &QAbstractItemModel::modelReset, this,
          [this] { m_selectionAnchor = QPersistentModelIndex{}; });
  m_changedId = m_service.changed.connect([this](const auto&) {
    invalidate();
    Q_EMIT editorChanged();
  });
  m_activeId =
      ctx.activeStateChanged().connect([this](const auto&) { invalidate(); });
  m_modsId = ctx.modService().modsChanged.connect(
      [this](const auto&) { invalidate(); });
  m_diagnosticsId = ctx.diagnosticsService().diagnosticsSummaryChanged.connect(
      [this](const auto&) {
        m_model->refreshDiagnostics();
        if (!m_selectedUuid.isEmpty())
          m_info->setIssues(
              m_ctx.diagnosticsService().issuesFor(m_selectedUuid));
      });
  m_busyId = ctx.downloads().busyChanged.connect([this](bool) { syncInfo(); });
}

OrganizerViewPanel::~OrganizerViewPanel() {
  m_service.changed.disconnect(m_changedId);
  m_ctx.activeStateChanged().disconnect(m_activeId);
  m_ctx.modService().modsChanged.disconnect(m_modsId);
  m_ctx.diagnosticsService().diagnosticsSummaryChanged.disconnect(
      m_diagnosticsId);
  m_ctx.downloads().busyChanged.disconnect(m_busyId);
}
bool OrganizerViewPanel::ready() const { return m_service.ready(); }
void OrganizerViewPanel::showEvent(QShowEvent* event) {
  BaseViewPanel::showEvent(event);
  if (m_dirty) rebuild();
  Q_EMIT readyChanged();
  if (auto* root = m_rulesQml->rootObject())
    QMetaObject::invokeMethod(root, "schedulePreview");
}
void OrganizerViewPanel::invalidate() {
  m_dirty = true;
  if (isVisible()) rebuild();
}
void OrganizerViewPanel::rebuild() {
  if (!ready()) return;
  m_dirty = false;
  m_filters->update(m_service.treeFilters());
  const auto query = m_filters->forKey(m_filterKey).query(m_searchText);
  const auto tree = m_service.tree(query);
  m_model->setTree(tree, m_ctx.allMods(), m_ctx.modService().providerColors(),
                   !query.isEmpty(), m_service.state().tags);
  const auto ungrouped =
      query.isEmpty() ? tree.mods.size() : m_service.tree().mods.size();
  m_status->setText(
      QStringLiteral("%1 · %2 active · %3 · %4 ungrouped")
          .arg(plural(m_ctx.allMods().size(), QStringLiteral("mod")))
          .arg(m_ctx.activeUuids().size())
          .arg(plural(m_service.state().folders.size() - 1,
                      QStringLiteral("folder")))
          .arg(ungrouped));
  syncInfo(true);
  Q_EMIT readyChanged();
}
QStringList OrganizerViewPanel::selectedPackageIds() const {
  QStringList result;
  for (const auto* node : m_model->selectedNodes())
    if (node->leaf && node->leaf->packageId &&
        !result.contains(*node->leaf->packageId))
      result.append(*node->leaf->packageId);
  return result;
}
QStringList OrganizerViewPanel::selectedUuids() const {
  QStringList result;
  for (const auto* node : m_model->selectedNodes())
    if (node->leaf) result.append(node->leaf->uuid);
  return result;
}
void OrganizerViewPanel::syncInfo(bool force) {
  const auto nodes = m_model->selectedNodes();
  const auto uuids = selectedUuids();
  Q_EMIT editorChanged();
  m_selectionLabel->setText(plural(uuids.size(), QStringLiteral("mod")) +
                            QStringLiteral(" selected"));
  m_selectionBar->setVisible(uuids.size() > 1);
  const auto state = updateState(m_ctx, uuids);
  m_updateButton->setVisible(m_ctx.downloads().available());
  m_updateButton->setText(state.label);
  m_updateButton->setEnabled(state.enabled);
  m_updateButton->setToolTip(state.tooltip);
  const QString uuid = nodes.size() == 1 && nodes.front()->leaf
                           ? nodes.front()->leaf->uuid
                           : QString{};
  if (uuid == m_selectedUuid && !force) return;
  m_selectedUuid = uuid;
  if (uuid.isEmpty())
    m_selection->clear();
  else
    m_selection->show(uuid);
}
int OrganizerViewPanel::selectedTagCount() const {
  return selectedPackageIds().size();
}
QVariantList OrganizerViewPanel::tagRows() const {
  QVariantList result;
  if (!ready()) return result;
  auto tags = m_service.state().tags.values();
  std::ranges::sort(tags, [](const Tag& a, const Tag& b) {
    return a.name.toCaseFolded() < b.name.toCaseFolded();
  });
  const auto pids = selectedPackageIds();
  for (const auto& tag : tags) {
    int matched = 0;
    for (const auto& pid : pids)
      if (m_service.state().modTags.value(pid).contains(tag.id)) ++matched;
    result.append(QVariantMap{
        {QStringLiteral("id"), tag.id},
        {QStringLiteral("name"), tag.name},
        {QStringLiteral("color"), tag.color},
        {QStringLiteral("checkState"),
         matched > 0 && matched < pids.size() ? 2 : int(matched > 0)}});
  }
  return result;
}
QVariantList OrganizerViewPanel::ruleRows() const {
  QVariantList result;
  if (!ready()) return result;
  for (const auto& rule : m_service.state().rules)
    result.append(QVariantMap{
        {QStringLiteral("field"), QString(ruleFieldName(rule.field))},
        {QStringLiteral("op"), QString(ruleOpName(rule.op))},
        {QStringLiteral("pattern"), rule.pattern},
        {QStringLiteral("folder_id"), rule.folderId}});
  return result;
}
QVariantList OrganizerViewPanel::ruleFolders() const {
  QVariantList result;
  if (!ready()) return result;
  const auto& folders = m_service.state().folders;
  QList<QPair<QString, qint64>> paths;
  paths.reserve(folders.size() - 1);
  for (const auto& folder : folders) {
    if (folder.id == kRootId) continue;
    QStringList names;
    qint64 id = folder.id;
    while (id != kRootId) {
      const auto& ancestor = *folders.constFind(id);
      names.prepend(ancestor.name);
      id = *ancestor.parentId;
    }
    paths.append({names.join(QStringLiteral(" / ")), folder.id});
  }
  std::ranges::sort(paths, [](const auto& a, const auto& b) {
    return QString::compare(a.first, b.first, Qt::CaseInsensitive) < 0;
  });
  result.reserve(paths.size());
  for (const auto& [name, id] : paths)
    result.append(QVariantMap{{QStringLiteral("id"), id},
                              {QStringLiteral("name"), name}});
  return result;
}
Result<QList<RuleSpec>> OrganizerViewPanel::ruleSpecs(
    const QVariantList& rows) const {
  QList<RuleSpec> specs;
  specs.reserve(rows.size());
  for (const auto& value : rows) {
    const auto row = value.toMap();
    const auto field =
        parseRuleField(row.value(QStringLiteral("field")).toString());
    const auto op = parseRuleOp(row.value(QStringLiteral("op")).toString());
    if (!field || !op)
      return std::unexpected(
          Error{QStringLiteral("Invalid rule field or operator")});
    specs.append(RuleSpec{*field, *op,
                          row.value(QStringLiteral("pattern")).toString(),
                          row.value(QStringLiteral("folder_id")).toLongLong()});
  }
  return specs;
}
QCoro::QmlTask OrganizerViewPanel::requestRulePreview(const QVariantList& rows,
                                                      int revision) {
  return calculateRulePreview(rows, revision);
}
QCoro::Task<void> OrganizerViewPanel::calculateRulePreview(QVariantList rows,
                                                           int revision) {
  if (!ready()) co_return;
  const auto specs = ruleSpecs(rows);
  if (!specs) co_return;
  const QPointer<OrganizerViewPanel> alive(this);
  auto future = QtConcurrent::run(
      [state = m_service.state(), mods = m_ctx.allMods(), specs = *specs] {
        return previewRuleMatches(state, mods, specs);
      });
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
  if (message != m_editorError) {
    m_editorError = std::move(message);
    Q_EMIT editorChanged();
  }
}
void OrganizerViewPanel::reportError(const Error& error) {
  if (auto* toasts = m_app.toasts())
    toasts->error(error.message);
  else
    spdlog::warn("organizer: {}", error.message.toStdString());
}
void OrganizerViewPanel::treeCommand(const char* method) {
  if (auto* root = m_qml->rootObject()) QMetaObject::invokeMethod(root, method);
}
void OrganizerViewPanel::openTags() {
  setEditorError();
  treeCommand("openTags");
}
void OrganizerViewPanel::openRules() {
  setEditorError();
  auto* root = m_rulesQml->rootObject();
  if (!root) return;
  auto* host = window();
  if (!m_rulesOpenedOnce) {
    const auto available = host->screen()->availableGeometry();
    m_rulesDialog->resize(std::min({960, std::max(800, host->width() - 48),
                                    available.width() - 32}),
                          std::min({760, std::max(600, host->height() - 80),
                                    available.height() - 32}));
    m_rulesOpenedOnce = true;
  }
  m_rulesDialog->move(host->frameGeometry().center() -
                      m_rulesDialog->rect().center());
  m_rulesDialog->open();
  m_rulesDialog->activateWindow();
  m_rulesQml->setFocus(Qt::OtherFocusReason);
  QMetaObject::invokeMethod(root, "prepareForOpen");
}
void OrganizerViewPanel::closeRules() { m_rulesDialog->close(); }
QCoro::QmlTask OrganizerViewPanel::createTag(QString name, QString color) {
  return saveNewTag(std::move(name), std::move(color));
}
QCoro::QmlTask OrganizerViewPanel::updateTag(qint64 id, QString name,
                                             QString color) {
  return saveTag(id, std::move(name), std::move(color));
}
QCoro::QmlTask OrganizerViewPanel::deleteTag(qint64 id) {
  return removeTag(id);
}
QCoro::QmlTask OrganizerViewPanel::assignTag(qint64 id, bool add) {
  return setTagAssigned(id, add);
}
QCoro::Task<void> OrganizerViewPanel::saveNewTag(QString name, QString color) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto result =
      co_await m_service.createTag(std::move(name), std::move(color));
  if (!alive) co_return;
  if (!result) {
    setEditorError(result.error().message);
    co_return;
  }
  Q_EMIT tagCreated();
  setEditorError();
}
QCoro::Task<void> OrganizerViewPanel::saveTag(qint64 id, QString name,
                                              QString color) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto result =
      co_await m_service.updateTag(id, std::move(name), std::move(color));
  if (!alive) co_return;
  if (!result) {
    setEditorError(result.error().message);
    co_return;
  }
  Q_EMIT tagUpdated(id);
  setEditorError();
}
QCoro::Task<void> OrganizerViewPanel::removeTag(qint64 id) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto result = co_await m_service.deleteTag(id);
  if (!alive) co_return;
  setEditorError(result ? QString{} : result.error().message);
}
QCoro::Task<void> OrganizerViewPanel::setTagAssigned(qint64 id, bool add) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto result = co_await m_service.setModTags(
      selectedPackageIds(), add ? QSet<qint64>{id} : QSet<qint64>{},
      add ? QSet<qint64>{} : QSet<qint64>{id});
  if (!alive) co_return;
  setEditorError(result ? QString{} : result.error().message);
}
QCoro::QmlTask OrganizerViewPanel::saveRules(QVariantList rows) {
  return persistRules(std::move(rows));
}
QCoro::Task<void> OrganizerViewPanel::persistRules(QVariantList rows) {
  const QPointer<OrganizerViewPanel> alive(this);
  auto specs = ruleSpecs(rows);
  if (!specs) {
    setEditorError(specs.error().message);
    co_return;
  }
  const auto result = co_await m_service.setRules(std::move(*specs));
  if (!alive) co_return;
  if (!result) {
    setEditorError(result.error().message);
    co_return;
  }
  setEditorError();
  Q_EMIT rulesSaved();
}
QCoro::QmlTask OrganizerViewPanel::addStandardRules() {
  return standardRules();
}
QCoro::Task<void> OrganizerViewPanel::standardRules() {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto result = co_await m_service.addStandardRules();
  if (!alive) co_return;
  setEditorError(result ? QString{} : result.error().message);
}
QVariantList OrganizerViewPanel::expandedIndexes() const {
  QVariantList result;
  for (const auto& index : m_model->expandedIndexes())
    result.append(QVariant::fromValue(index));
  return result;
}
void OrganizerViewPanel::selectRow(const QModelIndex& index, int modifiers,
                                   const QVariantList& range) {
  if ((modifiers & Qt::ShiftModifier) && m_selectionAnchor.isValid()) {
    QList<QModelIndex> indexes;
    for (const auto& value : range) indexes.append(value.value<QModelIndex>());
    m_model->select(indexes);
  } else {
    m_model->select({index}, modifiers & Qt::ControlModifier
                                 ? QStringLiteral("toggle")
                                 : QStringLiteral("replace"));
    m_selectionAnchor = index;
  }
  syncInfo();
}
void OrganizerViewPanel::selectSingle(const QModelIndex& index) {
  if (!m_model->isSelected(index)) m_model->select({index});
  m_selectionAnchor = index;
  syncInfo();
}
QCoro::QmlTask OrganizerViewPanel::persistAllCollapsed(bool collapsed) {
  return persistAllCollapse(collapsed);
}
QCoro::Task<void> OrganizerViewPanel::persistAllCollapse(bool collapsed) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto result = co_await m_service.setAllCollapsed(collapsed);
  if (!alive) co_return;
  if (!result) reportError(result.error());
}
QCoro::QmlTask OrganizerViewPanel::persistCollapsed(const QModelIndex& index,
                                                    bool collapsed) {
  return persistCollapse(index, collapsed);
}
QCoro::Task<void> OrganizerViewPanel::persistCollapse(QModelIndex index,
                                                      bool collapsed) {
  const auto* node = m_model->nodeAt(index);
  if (!node || !node->isContainer() || m_model->filtering()) co_return;
  const QPointer<OrganizerViewPanel> alive(this);
  const auto result =
      co_await m_service.setCollapsed(node->folderId(), collapsed);
  if (!alive) co_return;
  if (!result) reportError(result.error());
}
QCoro::QmlTask OrganizerViewPanel::toggleCheck(const QModelIndex& index) {
  return toggle(index);
}
QCoro::Task<void> OrganizerViewPanel::toggle(QModelIndex index) {
  const auto* node = m_model->nodeAt(index);
  if (!node) co_return;
  if (node->leaf) {
    const QStringList uuids{node->leaf->uuid};
    co_await toggleMods(m_ctx, this, uuids);
  } else if (node->isContainer()) {
    const auto change = m_service.folderToggle(
        node->folderId(), node->kind != QStringLiteral("ungrouped"));
    if (!change) {
      reportError(change.error());
      co_return;
    }
    if (!change->enable.isEmpty() || !change->disable.isEmpty())
      co_await applyActivation(m_ctx, this, change->enable, change->disable);
  }
}
QCoro::Task<void> OrganizerViewPanel::runSelectionAction(QString action) {
  const auto uuids = selectedUuids();
  if (uuids.isEmpty()) co_return;
  if (action == QStringLiteral("tags"))
    openTags();
  else if (action == QStringLiteral("move") ||
           action == QStringLiteral("create")) {
    const auto pids = selectedPackageIds();
    if (pids.isEmpty()) co_return;
    if (action == QStringLiteral("move"))
      co_await moveMods(pids);
    else
      co_await createFolderFrom(pids);
  } else if (action == QStringLiteral("update"))
    co_await updateMods(m_ctx, uuids, this);
  else if (action == QStringLiteral("enable"))
    co_await applyActivation(m_ctx, this, uuids);
  else if (action == QStringLiteral("disable"))
    co_await applyActivation(m_ctx, this, QStringList{}, uuids);
}
QCoro::Task<void> OrganizerViewPanel::newFolder() {
  co_await createFolder(kRootId);
}
QCoro::Task<std::optional<QString>> OrganizerViewPanel::askFolderName(
    QString title, QString initial, QString verb) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto [code, dialog] =
      co_await awaitDialog<FolderNameDialog>(title, initial, this, verb);
  if (!alive || code != QDialog::Accepted) co_return std::nullopt;
  co_return dialog->textValue();
}
QCoro::Task<std::optional<qint64>> OrganizerViewPanel::askFolder(
    QString title, QSet<qint64> allowed) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto [code, dialog] = co_await awaitDialog<FolderPickerDialog>(
      m_service.tree(), title, allowed, this);
  if (!alive || code != QDialog::Accepted) co_return std::nullopt;
  co_return dialog->folderId();
}
QCoro::Task<void> OrganizerViewPanel::createFolder(qint64 parent) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto name = co_await askFolderName(QStringLiteral("New Folder"), {},
                                           QStringLiteral("Create"));
  if (!name) co_return;
  const auto created = co_await m_service.createFolder(*name, parent);
  if (!alive) co_return;
  if (!created) reportError(created.error());
}
QCoro::Task<void> OrganizerViewPanel::createFolderFrom(QStringList pids) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto name =
      co_await askFolderName(QStringLiteral("Create Folder from Selection"), {},
                             QStringLiteral("Create"));
  if (!name) co_return;
  const auto created = co_await m_service.createFolderFrom(*name, pids);
  if (!alive) co_return;
  if (!created) reportError(created.error());
}
QCoro::Task<void> OrganizerViewPanel::renameFolder(qint64 id) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto name = co_await askFolderName(
      QStringLiteral("Rename Folder"), m_service.state().folders.value(id).name,
      QStringLiteral("Rename"));
  if (!name) co_return;
  const auto result = co_await m_service.renameFolder(id, *name);
  if (!alive) co_return;
  if (!result) reportError(result.error());
}
QCoro::Task<void> OrganizerViewPanel::moveMods(QStringList pids) {
  QSet<qint64> allowed;
  for (auto it = m_service.state().folders.cbegin();
       it != m_service.state().folders.cend(); ++it)
    allowed.insert(it.key());
  const auto destination =
      co_await askFolder(QStringLiteral("Move to Folder"), std::move(allowed));
  if (destination) co_await placeMods(pids, *destination);
}
QCoro::Task<void> OrganizerViewPanel::moveFolder(qint64 id) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto targets = m_service.folderMoveTargets(id);
  if (!targets) {
    reportError(targets.error());
    co_return;
  }
  const auto destination =
      co_await askFolder(QStringLiteral("Move Folder"), *targets);
  if (!destination) co_return;
  const auto result = co_await m_service.moveFolder(id, *destination);
  if (!alive) co_return;
  if (!result) reportError(result.error());
}
QCoro::Task<void> OrganizerViewPanel::deleteFolder(qint64 id) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto [code, dialog] = co_await awaitDialog<DeleteFolderDialog>(
      m_service.state().folders.value(id).name, this);
  if (!alive || code != QMessageBox::Yes) co_return;
  const auto result = co_await m_service.deleteFolder(id);
  if (!alive) co_return;
  if (!result) reportError(result.error());
}
QCoro::Task<void> OrganizerViewPanel::placeMods(QStringList pids,
                                                qint64 destination) {
  const QPointer<OrganizerViewPanel> alive(this);
  Result<void> result;
  if (destination == kRootId)
    result = co_await m_service.ungroup(pids);
  else
    result = co_await m_service.place(pids, destination);
  if (!alive) co_return;
  if (!result) reportError(result.error());
}
QCoro::Task<void> OrganizerViewPanel::resetRules(QStringList pids) {
  const QPointer<OrganizerViewPanel> alive(this);
  const auto result = co_await m_service.resetToRules(pids);
  if (!alive) co_return;
  if (!result) reportError(result.error());
}
void OrganizerViewPanel::contextMenu(const QModelIndex& index) {
  const auto* node = m_model->nodeAt(index);
  m_menu->clear();
  if (!node)
    m_menu->addAction(QStringLiteral("New folder…"), this,
                      &OrganizerViewPanel::newFolder);
  else if (node->leaf) {
    selectSingle(index);
    modMenu();
  } else {
    m_model->select({index});
    folderMenu(*node);
  }
  syncInfo();
  m_menu->popup(QCursor::pos());
}
void OrganizerViewPanel::modMenu() {
  const auto nodes = m_model->selectedNodes();
  const auto uuids = selectedUuids();
  bool inactive = false, active = false;
  for (const auto& uuid : uuids) {
    if (m_ctx.activeUuids().contains(uuid))
      active = true;
    else
      inactive = true;
  }
  if (inactive)
    m_menu->addAction(QStringLiteral("Enable"), this,
                      [this, uuids] { applyActivation(m_ctx, this, uuids); });
  if (active)
    m_menu->addAction(QStringLiteral("Disable"), this, [this, uuids] {
      applyActivation(m_ctx, this, {}, uuids);
    });
  addUpdateAction(m_menu, m_ctx, uuids,
                  [this, uuids] { updateMods(m_ctx, uuids, this); });
  m_menu->addSeparator();
  const auto pids = selectedPackageIds();
  bool placeable = !pids.isEmpty(), grouped = false;
  QStringList manual;
  for (const auto* node : nodes)
    if (node->leaf) {
      placeable &= node->leaf->packageId.has_value();
      grouped |= node->folderId() != kRootId;
      if (node->leaf->packageId &&
          node->leaf->placement == Placement::kManual &&
          !manual.contains(*node->leaf->packageId))
        manual.append(*node->leaf->packageId);
    }
  auto* move = m_menu->addAction(QStringLiteral("Move to folder…"), this,
                                 [this, pids] { moveMods(pids); });
  auto* remove = m_menu->addAction(QStringLiteral("Remove from folder"), this,
                                   [this, pids] { placeMods(pids, kRootId); });
  auto* create =
      m_menu->addAction(QStringLiteral("Create folder from selection…"), this,
                        [this, pids] { createFolderFrom(pids); });
  for (auto* action : {move, remove, create}) {
    action->setEnabled(placeable);
    action->setToolTip(
        placeable
            ? QString{}
            : QStringLiteral(
                  "Mods without a package ID cannot be placed in folders"));
  }
  remove->setEnabled(placeable && grouped);
  m_menu
      ->addAction(QStringLiteral("Manage tags…"), this,
                  &OrganizerViewPanel::openTags)
      ->setEnabled(placeable);
  if (placeable && !manual.isEmpty())
    m_menu->addAction(QStringLiteral("Use auto rules"), this,
                      [this, manual] { resetRules(manual); });
}
void OrganizerViewPanel::folderMenu(const TreeNode& node) {
  const auto id = node.folderId();
  const auto result =
      m_service.folderModUuids(id, node.kind != QStringLiteral("ungrouped"));
  if (!result) {
    reportError(result.error());
    return;
  }
  const auto& uuids = *result;
  m_menu
      ->addAction(QStringLiteral("Enable all"), this,
                  [this, uuids] { applyActivation(m_ctx, this, uuids); })
      ->setEnabled(!uuids.isEmpty());
  m_menu
      ->addAction(QStringLiteral("Disable all"), this,
                  [this, uuids] { applyActivation(m_ctx, this, {}, uuids); })
      ->setEnabled(!uuids.isEmpty());
  addUpdateAction(m_menu, m_ctx, uuids,
                  [this, uuids] { updateMods(m_ctx, uuids, this); });
  m_menu->addSeparator();
  if (node.kind == QStringLiteral("ungrouped")) {
    m_menu->addAction(QStringLiteral("New folder…"), this,
                      &OrganizerViewPanel::newFolder);
    return;
  }
  if (node.folder && node.folder->depth < kMaxDepth)
    m_menu->addAction(QStringLiteral("New subfolder…"), this,
                      [this, id] { createFolder(id); });
  m_menu->addAction(QStringLiteral("Rename…"), this,
                    [this, id] { renameFolder(id); });
  const auto targets = m_service.folderMoveTargets(id);
  m_menu
      ->addAction(QStringLiteral("Move to…"), this,
                  [this, id] { moveFolder(id); })
      ->setEnabled(targets && !targets->isEmpty());
  m_menu->addAction(QStringLiteral("Delete…"), this,
                    [this, id] { deleteFolder(id); });
}
bool OrganizerViewPanel::canDrop(const QModelIndex& source,
                                 const QModelIndex& target) const {
  const auto* src = m_model->nodeAt(source);
  const auto* dst = m_model->nodeAt(target);
  if (!src || !dst || !dst->isContainer() || src == dst) return false;
  if (src->leaf) {
    if (!m_model->isSelected(source)) return src->leaf->packageId.has_value();
    bool any = false;
    for (const auto* node : m_model->selectedNodes()) {
      if (!node->leaf) continue;
      any = true;
      if (!node->leaf->packageId) return false;
    }
    return any;
  }
  if (src->kind == QStringLiteral("folder")) {
    const auto targets = m_service.folderMoveTargets(src->folderId());
    return targets && targets->contains(dst->folderId());
  }
  return false;
}
QCoro::QmlTask OrganizerViewPanel::dropOn(const QModelIndex& source,
                                          const QModelIndex& target) {
  return performDrop(source, target);
}
QCoro::Task<void> OrganizerViewPanel::performDrop(QModelIndex source,
                                                  QModelIndex target) {
  const QPointer<OrganizerViewPanel> alive(this);
  if (!canDrop(source, target)) co_return;
  const auto* src = m_model->nodeAt(source);
  const auto* dst = m_model->nodeAt(target);
  const auto destination = dst->folderId();
  if (src->kind == QStringLiteral("folder")) {
    const auto result =
        co_await m_service.moveFolder(src->folderId(), destination);
    if (!alive) co_return;
    if (!result) reportError(result.error());
  } else {
    const auto pids = m_model->isSelected(source)
                          ? selectedPackageIds()
                          : QStringList{*src->leaf->packageId};
    co_await placeMods(pids, destination);
  }
}
}  // namespace px::modrim
