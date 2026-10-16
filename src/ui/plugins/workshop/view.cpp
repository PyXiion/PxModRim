#include "ui/plugins/workshop/view.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQuickWidget>
#include <QUrl>
#include <algorithm>

#include <spdlog/spdlog.h>

#include "ui/components/mod_activation.hpp"
#include "ui/navigation.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim::workshop {
using namespace workshop;
namespace {
QString idOf(const CatalogEntry& item) {
  return std::visit(
      [](const auto& value) { return QString::fromStdString(value.id); }, item);
}
QSet<QString> idsOf(const std::vector<std::string>& ids) {
  QSet<QString> result;
  for (const auto& id : ids) result.insert(QString::fromStdString(id));
  return result;
}
std::optional<QString> cursorOf(const std::optional<std::string>& cursor) {
  return cursor ? std::optional(QString::fromStdString(*cursor)) : std::nullopt;
}
WorkshopCatalog& catalogOf(CoreContext& ctx) {
  return *static_cast<WorkshopCatalog*>(
      ctx.plugins().get(QStringLiteral("workshop_catalog")));
}
}  // namespace

WorkshopViewPanel::WorkshopViewPanel(CoreContext& ctx, QQmlEngine* engine,
                                     QWidget* parent, AppContext& app)
    : WorkshopViewPanel(ctx, engine, parent, app, catalogOf(ctx)) {}

WorkshopViewPanel::WorkshopViewPanel(CoreContext& ctx, QQmlEngine* engine,
                                     QWidget* parent, AppContext& app,
                                     WorkshopCatalog& catalog)
    : BaseViewPanel(ctx, engine, parent, app),
      modsModel(new CatalogListModel(this)),
      collectionsModel(new CatalogListModel(this)),
      membersModel(new CatalogListModel(this)),
      m_catalog(catalog),
      m_downloads(ctx.downloads()),
      m_favourites(catalog.favourites()) {
  if (!gameVersion().isEmpty()) m_criteria.version = gameVersion();
  refreshModIds();
  m_qml = new QQuickWidget(engine, this);
  m_qml->setObjectName(QStringLiteral("workshopView"));
  m_qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
  m_qml->setClearColor(px::modrim::paletteColor(u"ELEVATE_0"));
  m_qml->rootContext()->setContextProperty(QStringLiteral("workshopPanel"),
                                           this);
  m_qml->rootContext()->setContextProperty(QStringLiteral("catalogMods"),
                                           modsModel);
  m_qml->rootContext()->setContextProperty(QStringLiteral("catalogCollections"),
                                           collectionsModel);
  m_qml->rootContext()->setContextProperty(QStringLiteral("catalogMembers"),
                                           membersModel);
  m_busyId = m_downloads.busyChanged.connect([this](bool) {
    refreshRows();
    emit changed();
  });
  m_queueId = m_catalog.queueChanged.connect([this](const std::monostate&) {
    refreshRows();
    emit changed();
  });
  m_urlId = m_catalog.catalogUrlChanged.connect(
      [this](const QString&) { urlChanged(); });
  m_installedId = m_catalog.installedChanged.connect(
      [this](const std::monostate&) { installedChanged(); });
  m_favouritesId =
      m_favourites.changed().connect([this](const FavouritesState&) {
        if (m_tab == QStringLiteral("Favourites") && !m_detailData)
          showFavourites();
        else {
          refreshRows();
          emit changed();
        }
      });
  m_activeId = m_ctx.activeStateChanged().connect([this](const QStringList&) {
    refreshModIds();
    refreshRows();
    emit changed();
  });
  m_qml->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/Workshop.qml")));
  m_root->addWidget(m_qml, 1);
}

WorkshopViewPanel::~WorkshopViewPanel() {
  teardown();
  // Workshop.qml bindings read workshopPanel; the view must die while this
  // panel is still alive.
  delete m_qml;
}
void WorkshopViewPanel::teardown() {
  if (m_tornDown) return;
  m_tornDown = true;
  ++m_generation;
  m_retry = {};
  m_downloads.busyChanged.disconnect(m_busyId);
  m_catalog.queueChanged.disconnect(m_queueId);
  m_catalog.catalogUrlChanged.disconnect(m_urlId);
  m_catalog.installedChanged.disconnect(m_installedId);
  m_favourites.changed().disconnect(m_favouritesId);
  m_ctx.activeStateChanged().disconnect(m_activeId);
}

QString WorkshopViewPanel::gameVersion() const {
  const auto version = m_ctx.targetVersion();
  return version == QStringLiteral("Unknown") ? QString{} : version;
}
void WorkshopViewPanel::refreshModIds() {
  m_installedIds.clear();
  m_activeIds.clear();
  const auto& mods = m_ctx.allMods();
  for (const auto& mod : mods)
    if (auto id = mod->publishedFileId()) m_installedIds.insert(*id);
  for (const auto& uuid : m_ctx.activeUuids()) {
    auto mod = mods.value(uuid);
    if (mod)
      if (auto id = mod->publishedFileId()) m_activeIds.insert(*id);
  }
}

QVariantMap WorkshopViewPanel::row(const CatalogEntry& item, bool detail) {
  const auto id = idOf(item);
  m_items.insert(id, item);
  std::visit(
      [this](const auto& value) {
        auto tags = idsOf(value.tags);
        tags.subtract(idsOf(value.supported_versions));
        m_knownTags.unite(tags);
      },
      item);
  auto result = itemRow(
      item,
      [this](const CatalogMod& mod) { return m_catalog.installState(mod); },
      gameVersion(), detail);
  const auto* mod = std::get_if<CatalogMod>(&item);
  const auto* collection = std::get_if<CatalogCollection>(&item);
  result[QStringLiteral("active")] = mod && m_activeIds.contains(id);
  if (collection) {
    if (!detail) {
      qsizetype count = 0;
      for (const auto& member : collection->member_ids)
        count += m_installedIds.contains(QString::fromStdString(member));
      if (!collection->member_ids.empty() &&
          count == static_cast<qsizetype>(collection->member_ids.size())) {
        result[QStringLiteral("state")] = QStringLiteral("installed");
        result[QStringLiteral("stateLabel")] = QStringLiteral("Installed");
        result[QStringLiteral("actionLabel")] = QStringLiteral("Installed");
      } else if (count)
        result[QStringLiteral("actionLabel")] =
            QStringLiteral("Complete download");
    }
    if (m_favourites.contains(collection->id)) {
      result[QStringLiteral("favourite")] = true;
      m_favourites.remember(*collection);
    }
  }
  if (collection || result.value(QStringLiteral("state")).toString() !=
                        QStringLiteral("installed")) {
    if (m_preparing.contains(id)) {
      result[QStringLiteral("queued")] = true;
      result[QStringLiteral("actionLabel")] = QStringLiteral("Preparing…");
    } else {
      QSet<QString> ids =
          mod ? QSet<QString>{id}
              : idsOf(collection->member_ids) | m_collectionMods.value(id);
      if (collection && m_detailData && idOf(m_detailData->item) == id)
        ids.unite(m_packIds);
      if (ids.intersects(m_downloads.activeIds())) {
        result[QStringLiteral("queued")] = true;
        result[QStringLiteral("actionLabel")] = QStringLiteral("Downloading");
      } else if (ids.intersects(m_catalog.queuedIds())) {
        result[QStringLiteral("queued")] = true;
        result[QStringLiteral("actionLabel")] = QStringLiteral("Queued");
      }
    }
  }
  return result;
}

QVariantMap WorkshopViewPanel::detailRow(const Detail& detail) {
  auto result = row(detail.item, true);
  result[QStringLiteral("complete")] = detail.complete;
  result[QStringLiteral("warning")] = QString::fromStdString(detail.warning);
  if (std::holds_alternative<CatalogCollection>(detail.item)) {
    auto collage = result.value(QStringLiteral("collage")).toStringList();
    if (result.value(QStringLiteral("previewUrl")).toString().isEmpty() &&
        collage.isEmpty()) {
      for (const auto& member : detail.members) {
        const auto* mod = std::get_if<CatalogMod>(&member);
        if (!mod) continue;
        QString preview = QString::fromStdString(mod->preview_url.value_or(""));
        if (preview.isEmpty())
          for (const auto& image : mod->previews) {
            if (image.type == PreviewType::kImage) {
              preview = QString::fromStdString(image.url);
              break;
            }
          }
        preview = safeUrl(preview);
        if (!preview.isEmpty()) collage.append(preview);
        if (collage.size() == 4) break;
      }
      result[QStringLiteral("collage")] = collage;
    }
    bool started = false;
    for (const auto& member : detail.members)
      if (auto* mod = std::get_if<CatalogMod>(&member))
        started |= m_catalog.installState(*mod) != InstallState::kMissing;
    if (!result.value(QStringLiteral("queued")).toBool())
      result[QStringLiteral("actionLabel")] =
          started ? QStringLiteral("Complete download")
                  : QStringLiteral("Download all");
    if (auto copies = packCopies()) {
      result[QStringLiteral("state")] = QStringLiteral("installed");
      result[QStringLiteral("stateLabel")] = QStringLiteral("Installed");
      if (!result.value(QStringLiteral("queued")).toBool())
        result[QStringLiteral("actionLabel")] = QStringLiteral("Installed");
      result[QStringLiteral("active")] =
          std::ranges::all_of(*copies, [this](const auto& uuid) {
            return m_ctx.activeUuids().contains(uuid);
          });
    }
  }
  return result;
}
void WorkshopViewPanel::refreshRows() {
  for (auto* model : {modsModel, collectionsModel, membersModel})
    model->refreshRows([this](const QVariantMap& value) {
      return row(
          m_items.value(value.value(QStringLiteral("itemId")).toString()));
    });
  if (m_detailData) m_detail = detailRow(*m_detailData);
}
void WorkshopViewPanel::clearDetail() {
  m_detail.clear();
  m_detailData.reset();
  m_current.reset();
  m_trail.clear();
  m_packIds.clear();
}
std::optional<QStringList> WorkshopViewPanel::packCopies() const {
  if (m_tornDown || m_packIds.isEmpty()) return std::nullopt;
  QHash<QString, QStringList> installed;
  for (auto it = m_ctx.allMods().cbegin(); it != m_ctx.allMods().cend(); ++it)
    if (auto id = it.value()->publishedFileId(); id && m_packIds.contains(*id))
      installed[*id].append(it.key());
  if (installed.size() < m_packIds.size()) return std::nullopt;
  QStringList copies;
  for (const auto& uuids : installed) {
    auto active = std::ranges::find_if(uuids, [this](const auto& uuid) {
      return m_ctx.activeUuids().contains(uuid);
    });
    copies.append(active == uuids.cend() ? uuids.front() : *active);
  }
  return copies;
}
QCoro::Task<void> WorkshopViewPanel::loadPack() {
  if (!m_detailData || m_tornDown) co_return;
  QPointer<WorkshopViewPanel> self(this);
  const int generation = m_generation;
  auto result = co_await m_catalog.plan({}, {idOf(m_detailData->item)});
  if (!self || self->m_tornDown || generation != self->m_generation) co_return;
  if (!result) {
    spdlog::warn("[workshop] cannot resolve collection mods: {}",
                 result.error().message.toStdString());
    co_return;
  }
  self->m_packIds.clear();
  if (result->is_complete) {
    self->m_packIds =
        QSet<QString>(result->to_download.cbegin(), result->to_download.cend());
    self->m_packIds.unite(QSet<QString>(result->already_current.cbegin(),
                                        result->already_current.cend()));
  }
  self->refreshRows();
  emit self->changed();
}

QCoro::Task<void> WorkshopViewPanel::load(bool append) {
  if (m_tornDown) co_return;
  if (!configured()) {
    ++m_generation;
    m_busy = false;
    emit changed();
    co_return;
  }
  clearDetail();
  auto* catalog = &m_catalog;
  if (m_tab == QStringLiteral("Discover")) {
    co_await runCached<Discover>(
        QStringLiteral("discover"), [catalog] { return catalog->discover(); },
        [this](const Discover& value) {
          modsModel->setPage(rows(value.mods.items),
                             static_cast<int>(value.mods.total), std::nullopt);
          collectionsModel->setPage(rows(value.collections.items),
                                    static_cast<int>(value.collections.total),
                                    std::nullopt);
        });
  } else if (m_tab == QStringLiteral("Favourites")) {
    ++m_generation;
    m_busy = false;
    m_error.clear();
    showFavourites();
  } else if (m_tab == QStringLiteral("Installed")) {
    if (append) {
      modsModel->setPage(rows(m_installed.nextPage()), m_installed.total(),
                         installedCursor(), true);
      co_return;
    }
    m_installedDirty = false;
    auto ids = m_installedIds.values();
    std::ranges::sort(ids);
    const auto fingerprint =
        QCryptographicHash::hash(ids.join(QLatin1Char(',')).toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex();
    co_await runCached<std::vector<CatalogMod>>(
        QStringLiteral("installed|") + QString::fromLatin1(fingerprint),
        [catalog] { return catalog->installedWithUpdates(); },
        [this](const std::vector<CatalogMod>& value) {
          m_installed.replace(value);
          recount();
          showInstalled();
        });
  } else {
    const bool collections = m_tab == QStringLiteral("Collections");
    auto* model = collections ? collectionsModel : modsModel;
    auto query = m_criteria;
    query.cursor = append ? model->cursor() : std::nullopt;
    const QString key =
        m_tab + QLatin1Char('|') +
        QString::fromLatin1(
            QCryptographicHash::hash(
                QJsonDocument(QJsonArray{query.query,
                                         query.tag.value_or(QString{}),
                                         query.version.value_or(QString{}),
                                         query.sort, query.limit, query.source})
                    .toJson(QJsonDocument::Compact),
                QCryptographicHash::Sha256)
                .toHex());
    if (collections) {
      co_await runCached<CatalogPage<CatalogCollection>>(
          key, [catalog, query] { return catalog->collections(query); },
          [this, model, append](const CatalogPage<CatalogCollection>& value) {
            model->setPage(rows(value.items), static_cast<int>(value.total),
                           cursorOf(value.next_cursor), append);
          },
          !append);
    } else {
      co_await runCached<CatalogPage<CatalogMod>>(
          key, [catalog, query] { return catalog->mods(query); },
          [this, model, append](const CatalogPage<CatalogMod>& value) {
            model->setPage(rows(value.items), static_cast<int>(value.total),
                           cursorOf(value.next_cursor), append);
          },
          !append);
    }
  }
}

void WorkshopViewPanel::showFavourites() {
  std::vector<CatalogCollection> matches;
  const auto query = m_criteria.query.toCaseFolded();
  for (const auto& item : m_favourites.items())
    if (QString::fromStdString(item.title).toCaseFolded().contains(query))
      matches.push_back(item);
  collectionsModel->setPage(rows(matches), static_cast<int>(matches.size()),
                            std::nullopt);
  emit changed();
}
std::optional<QString> WorkshopViewPanel::installedCursor() const {
  return m_installed.hasMore() ? std::optional(QStringLiteral("more"))
                               : std::nullopt;
}
void WorkshopViewPanel::showInstalled() {
  clearDetail();
  modsModel->setPage(rows(m_installed.visible()), m_installed.total(),
                     installedCursor());
  emit changed();
}
void WorkshopViewPanel::recount() {
  m_updateCount = 0;
  for (const auto& mod : m_installed.mods())
    m_updateCount += m_catalog.installState(mod) == InstallState::kOutdated;
}

QCoro::Task<void> WorkshopViewPanel::selectTabAsync(QString tab) {
  m_tab = std::move(tab);
  m_notice.clear();
  m_pending.reset();
  clearDetail();
  m_installed.resetPaging();
  co_await load();
}
QCoro::Task<void> WorkshopViewPanel::filterItems(QString query, QString version,
                                                 QString source, QString sort,
                                                 QString tag) {
  m_criteria = CatalogQuery{
      .query = query,
      .tag = tag.isEmpty() ? std::nullopt : std::optional(tag),
      .version = version.isEmpty() ? std::nullopt : std::optional(version),
      .sort = sort,
      .source = source};
  m_installed.search(query);
  if (m_tab == QStringLiteral("Installed") && m_installed.loaded()) {
    showInstalled();
    co_return;
  }
  if (m_tab == QStringLiteral("Discover")) m_tab = QStringLiteral("Mods");
  co_await load();
}
QCoro::Task<void> WorkshopViewPanel::retryLoad() {
  if (m_retry && !m_tornDown) {
    auto action = m_retry;
    co_await action();
  }
  co_return;
}
void WorkshopViewPanel::go(const QStringList& path) {
  m_app.navigate(buildRoute(QStringLiteral("workshop"), path));
}
void WorkshopViewPanel::selectTab(const QString& tab) { go({tab.toLower()}); }
void WorkshopViewPanel::openItem(const QString& itemId, const QString& kind) {
  go({kind, itemId});
}
void WorkshopViewPanel::openRoute(const QStringList& path) {
  followRoute(path);
}
QCoro::Task<void> WorkshopViewPanel::followRoute(QStringList path) {
  if (m_tornDown || path.isEmpty()) co_return;
  const QStringList tabs{QStringLiteral("Discover"), QStringLiteral("Mods"),
                         QStringLiteral("Collections"),
                         QStringLiteral("Favourites"),
                         QStringLiteral("Installed")};
  if (path.size() == 1) {
    for (const auto& tab : tabs)
      if (tab.compare(path.front(), Qt::CaseInsensitive) == 0) {
        co_await selectTabAsync(tab);
        co_return;
      }
  } else if (path.size() == 2 &&
             (path.front() == QStringLiteral("mod") ||
              path.front() == QStringLiteral("collection"))) {
    co_await openItemAsync(path[1], path[0]);
    co_return;
  }
  spdlog::warn("[workshop] unknown route {}",
               buildRoute(QStringLiteral("workshop"), path).toStdString());
}
QCoro::Task<void> WorkshopViewPanel::openItemAsync(QString itemId, QString kind,
                                                   bool record) {
  m_packIds.clear();
  QPointer<WorkshopViewPanel> self(this);
  auto* catalog = &m_catalog;
  co_await runCached<Detail>(
      QStringLiteral("detail|") + kind + QLatin1Char('|') + itemId,
      [catalog, itemId, kind] { return fetchDetail(*catalog, itemId, kind); },
      [this, itemId, kind, record](const Detail& value) {
        const auto ref = std::pair(kind, itemId);
        if (m_current != ref) {
          if (m_detail.isEmpty())
            m_trail.clear();
          else if (record && m_current)
            m_trail.emplace_back(
                m_current->first, m_current->second,
                m_detail.value(QStringLiteral("title")).toString());
        }
        m_current = ref;
        auto members = rows(value.members);
        m_detailData = value;
        m_detail = detailRow(value);
        membersModel->setPage(std::move(members),
                              static_cast<int>(value.members.size()),
                              std::nullopt);
      });
  if (self && !self->m_tornDown && kind == QStringLiteral("collection") &&
      self->m_current == std::pair(kind, itemId))
    co_await self->loadPack();
}
QCoro::Task<void> WorkshopViewPanel::goBack() {
  if (m_tornDown) co_return;
  if (!m_trail.empty()) {
    auto [kind, item_id, title] = m_trail.back();
    QPointer<WorkshopViewPanel> self(this);
    co_await openItemAsync(item_id, kind, false);
    if (self && !self->m_tornDown &&
        self->m_current == std::pair(kind, item_id) && !self->m_trail.empty()) {
      self->m_trail.pop_back();
      emit self->changed();
    }
    co_return;
  }
  ++m_generation;
  clearDetail();
  m_error.clear();
  m_busy = false;
  emit changed();
  if (m_tab == QStringLiteral("Favourites"))
    showFavourites();
  else if (m_tab == QStringLiteral("Installed") && m_installedDirty)
    co_await load();
}

QCoro::Task<void> WorkshopViewPanel::toggleActivationAsync(QString itemId) {
  if (m_tornDown) co_return;
  QStringList copies, active;
  for (auto it = m_ctx.allMods().cbegin(); it != m_ctx.allMods().cend(); ++it) {
    if (it.value()->publishedFileId() != itemId) continue;
    copies.append(it.key());
    if (m_ctx.activeUuids().contains(it.key())) active.append(it.key());
  }
  if (copies.isEmpty()) co_return;
  if (active.isEmpty())
    co_await applyActivation(m_ctx, this, {copies.front()}, {});
  else
    co_await applyActivation(m_ctx, this, {}, active);
}
QCoro::Task<void> WorkshopViewPanel::toggleCollectionAsync(bool only) {
  auto copies = packCopies();
  if (!copies) co_return;
  QStringList disable;
  const bool allActive = std::ranges::all_of(*copies, [this](const auto& uuid) {
    return m_ctx.activeUuids().contains(uuid);
  });
  for (const auto& uuid : m_ctx.activeUuids()) {
    auto mod = m_ctx.allMods().value(uuid);
    if (!mod) continue;
    auto id = mod->publishedFileId();
    if (id && ((only && !copies->contains(uuid)) ||
               (!only && allActive && m_packIds.contains(*id))))
      disable.append(uuid);
  }
  if (!only && allActive)
    co_await applyActivation(m_ctx, this, {}, disable);
  else
    co_await applyActivation(m_ctx, this, *copies, disable);
}
QCoro::Task<void> WorkshopViewPanel::downloadItemAsync(QString itemId,
                                                       QString kind) {
  co_await download(
      kind == QStringLiteral("mod") ? QStringList{itemId} : QStringList{},
      kind == QStringLiteral("collection") ? QStringList{itemId}
                                           : QStringList{});
}
QCoro::Task<void> WorkshopViewPanel::updateAllAsync() {
  QStringList ids;
  for (const auto& mod : m_installed.mods())
    if (m_catalog.installState(mod) == InstallState::kOutdated)
      ids.append(QString::fromStdString(mod.id));
  if (!ids.isEmpty()) co_await download(ids, {});
}
void WorkshopViewPanel::queuePlan(const DownloadPlan& plan) {
  m_catalog.enqueue(plan);
  m_notice = queuedNotice(plan.to_download.size());
}
QCoro::Task<void> WorkshopViewPanel::download(QStringList ids,
                                              QStringList collections) {
  if (m_tornDown) co_return;
  QPointer<WorkshopViewPanel> self(this);
  m_notice = QStringLiteral("Preparing download…");
  m_pending.reset();
  ++m_planning;
  auto requested = QSet<QString>(ids.cbegin(), ids.cend());
  requested.unite(QSet<QString>(collections.cbegin(), collections.cend()));
  m_preparing.unite(requested);
  refreshRows();
  emit changed();
  auto result = co_await m_catalog.plan(ids, collections);
  if (!self || self->m_tornDown) co_return;
  --self->m_planning;
  self->m_preparing.subtract(requested);
  self->refreshRows();
  emit self->changed();
  if (!result) {
    self->m_notice = QStringLiteral("Could not prepare the download: ") +
                     result.error().message;
    emit self->changed();
    co_return;
  }
  for (const auto& collection : collections)
    self->m_collectionMods[collection] =
        QSet<QString>(result->to_download.cbegin(), result->to_download.cend());
  if (!result->is_complete) {
    self->m_pending = *result;
    self->m_notice = incompletePlanNotice(*result);
  } else if (result->to_download.isEmpty())
    self->m_notice = QStringLiteral(
        "All available mods are already installed and up to date.");
  else
    self->queuePlan(*result);
  emit self->changed();
}
void WorkshopViewPanel::downloadAvailable() {
  if (m_tornDown) return;
  auto plan = std::exchange(m_pending, std::nullopt);
  if (plan && !plan->to_download.isEmpty()) queuePlan(*plan);
  emit changed();
}
void WorkshopViewPanel::dismissPlan() {
  m_pending.reset();
  m_notice.clear();
  emit changed();
}
void WorkshopViewPanel::openLink(const QString& url) {
  if (!safeUrl(url).isEmpty()) QDesktopServices::openUrl(QUrl(url));
}
void WorkshopViewPanel::openSettings() {
  m_app.navigate(buildRoute(QString(kSettingsViewId)));
}
void WorkshopViewPanel::toggleFavourite(const QString& itemId) {
  const auto found = m_items.constFind(itemId);
  if (found != m_items.cend())
    if (const auto* collection = std::get_if<CatalogCollection>(&*found))
      m_favourites.set(*collection, !m_favourites.contains(collection->id));
}
QCoro::Task<void> WorkshopViewPanel::urlChanged() {
  if (m_tornDown) co_return;
  clearDetail();
  m_error.clear();
  m_notice.clear();
  m_pending.reset();
  m_items.clear();
  m_knownTags.clear();
  for (auto* model : {modsModel, collectionsModel, membersModel})
    model->setPage({}, 0, std::nullopt);
  co_await load();
}
QCoro::Task<void> WorkshopViewPanel::installedChanged() {
  if (m_tornDown) co_return;
  m_installedDirty = true;
  refreshModIds();
  recount();
  refreshRows();
  emit changed();
  if (m_tab == QStringLiteral("Installed") && !m_busy && !m_detailData)
    co_await load();
}
QString WorkshopViewPanel::url() const {
  return buildRoute(QStringLiteral("workshop"),
                    m_current ? QStringList{m_current->first, m_current->second}
                              : QStringList{m_tab.toLower()});
}
QString WorkshopViewPanel::backLabel() const {
  return QStringLiteral("Back to ") +
         (m_trail.empty() ? m_tab : std::get<2>(m_trail.back()));
}
QStringList WorkshopViewPanel::tagOptions() const {
  auto tags = m_knownTags.values();
  std::ranges::sort(tags);
  tags.prepend(QStringLiteral("All tags"));
  return tags;
}
QStringList WorkshopViewPanel::versionOptions() const {
  QStringList versions{QStringLiteral("All versions")};
  if (m_criteria.version) versions.append(*m_criteria.version);
  for (const auto& version : {QStringLiteral("1.6"), QStringLiteral("1.5"),
                              QStringLiteral("1.4"), QStringLiteral("1.3")})
    if (!versions.contains(version)) versions.append(version);
  return versions;
}

}  // namespace px::modrim::workshop
