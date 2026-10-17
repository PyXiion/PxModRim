#pragma once

#include <QPointer>
#include <QSet>
#include <QVariantMap>
#include <functional>
#include <optional>
#include <tuple>

#include <QCoro/QCoroQmlTask>

#include "ui/plugins/workshop/details.hpp"
#include "ui/plugins/workshop/installed.hpp"
#include "ui/plugins/workshop/models.hpp"
#include "ui/views/base.hpp"

class QQuickWidget;

namespace px::modrim::workshop {

class WorkshopViewPanel : public BaseViewPanel {
  Q_OBJECT
  Q_PROPERTY(QString tab READ tab NOTIFY changed)
  Q_PROPERTY(QString url READ url NOTIFY changed)
  Q_PROPERTY(QString backLabel READ backLabel NOTIFY changed)
  Q_PROPERTY(bool configured READ configured NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString notice READ notice NOTIFY changed)
  Q_PROPERTY(QVariantMap detail READ detail NOTIFY changed)
  Q_PROPERTY(bool hasDetail READ hasDetail NOTIFY changed)
  Q_PROPERTY(bool pendingPlan READ pendingPlan NOTIFY changed)
  Q_PROPERTY(bool canDownloadAvailable READ canDownloadAvailable NOTIFY changed)
  Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY changed)
  Q_PROPERTY(QStringList tagOptions READ tagOptions NOTIFY changed)
  Q_PROPERTY(QString gameVersion READ gameVersion CONSTANT)
  Q_PROPERTY(QStringList versionOptions READ versionOptions CONSTANT)
  Q_PROPERTY(int updateCount READ updateCount NOTIFY changed)
  Q_PROPERTY(bool planning READ planning NOTIFY changed)

 public:
  static constexpr RailViewMeta kRailMeta{QLatin1StringView("workshop"),
                                          QLatin1StringView("steam"),
                                          QLatin1StringView("Workshop")};
  WorkshopViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
                    AppContext& app);
  WorkshopViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
                    AppContext& app, workshop::WorkshopCatalog& catalog);
  ~WorkshopViewPanel() override;
  void openRoute(const QStringList& path) override;
  Q_INVOKABLE void teardown() override;
  QCoro::Task<void> followRoute(QStringList path);
  QCoro::Task<void> load(bool append = false);
  QCoro::Task<void> selectTabAsync(QString tab);
  QCoro::Task<void> openItemAsync(QString itemId, QString kind,
                                  bool record = true);
  QCoro::Task<void> filterItems(QString query, QString version, QString source,
                                QString sort, QString tag);
  QCoro::Task<void> goBack();
  QCoro::Task<void> retryLoad();
  QCoro::Task<void> downloadItemAsync(QString itemId, QString kind);
  QCoro::Task<void> toggleActivationAsync(QString itemId);
  QCoro::Task<void> toggleCollectionAsync(bool only = false);
  QCoro::Task<void> updateAllAsync();

  Q_INVOKABLE QCoro::QmlTask refresh() { return load(); }
  Q_INVOKABLE void selectTab(const QString& tab);
  Q_INVOKABLE QCoro::QmlTask filter(QString query, QString version,
                                    QString source, QString sort, QString tag) {
    return filterItems(std::move(query), std::move(version), std::move(source),
                       std::move(sort), std::move(tag));
  }
  Q_INVOKABLE QCoro::QmlTask loadMore() { return load(true); }
  Q_INVOKABLE QCoro::QmlTask retry() { return retryLoad(); }
  Q_INVOKABLE void openItem(const QString& itemId, const QString& kind);
  Q_INVOKABLE QCoro::QmlTask back() { return goBack(); }
  Q_INVOKABLE QCoro::QmlTask downloadItem(QString itemId, QString kind) {
    return downloadItemAsync(std::move(itemId), std::move(kind));
  }
  Q_INVOKABLE QCoro::QmlTask toggleActivation(QString itemId) {
    return toggleActivationAsync(std::move(itemId));
  }
  Q_INVOKABLE QCoro::QmlTask toggleCollection() {
    return toggleCollectionAsync();
  }
  Q_INVOKABLE QCoro::QmlTask activateOnlyCollection() {
    return toggleCollectionAsync(true);
  }
  Q_INVOKABLE QCoro::QmlTask updateAll() { return updateAllAsync(); }
  Q_INVOKABLE void downloadAvailable();
  Q_INVOKABLE void dismissPlan();
  Q_INVOKABLE void openLink(const QString& url);
  Q_INVOKABLE void openSettings();
  Q_INVOKABLE void toggleFavourite(const QString& itemId);

  QString tab() const { return m_tab; }
  QString url() const;
  QString backLabel() const;
  bool configured() const { return m_catalog.configured(); }
  bool busy() const { return m_busy; }
  QString error() const { return m_error; }
  QString notice() const { return m_notice; }
  QVariantMap detail() const { return m_detail; }
  bool hasDetail() const { return !m_detail.isEmpty(); }
  bool pendingPlan() const { return m_pending.has_value(); }
  bool canDownloadAvailable() const {
    return m_pending && !m_pending->to_download.isEmpty();
  }
  QString searchQuery() const { return m_criteria.query; }
  QStringList tagOptions() const;
  QString gameVersion() const;
  QStringList versionOptions() const;
  int updateCount() const { return m_updateCount; }
  bool planning() const { return m_planning > 0; }

  CatalogListModel* modsModel;
  CatalogListModel* collectionsModel;
  CatalogListModel* membersModel;

 signals:
  void changed();

 private:
  template <class T>
  QCoro::Task<void> runCached(
      QString key,
      std::function<QCoro::Task<workshop::CatalogResult<T>>()> produce,
      std::function<void(const T&)> apply, bool cached = true);
  QCoro::Task<void> loadPack();
  QCoro::Task<void> download(QStringList ids, QStringList collections);
  QCoro::Task<void> installedChanged();
  QCoro::Task<void> urlChanged();
  void refreshModIds();
  QVariantMap row(const workshop::CatalogEntry& item, bool detail = false);
  QVariantMap detailRow(const Detail& detail);
  template <class T>
  QList<QVariantMap> rows(const std::vector<T>& items) {
    refreshModIds();
    QList<QVariantMap> result;
    result.reserve(static_cast<qsizetype>(items.size()));
    for (const auto& item : items) result.append(row(item));
    return result;
  }
  void refreshRows();
  void clearDetail();
  std::optional<QStringList> packCopies() const;
  void showFavourites();
  void showInstalled();
  void recount();
  std::optional<QString> installedCursor() const;
  void queuePlan(const workshop::DownloadPlan& plan);
  void go(const QStringList& path);

  workshop::WorkshopCatalog& m_catalog;
  DownloadManager& m_downloads;
  workshop::FavouriteCollections& m_favourites;
  QQuickWidget* m_qml = nullptr;
  QString m_tab = QStringLiteral("Discover");
  workshop::CatalogQuery m_criteria;
  bool m_busy = false;
  bool m_tornDown = false;
  bool m_installedDirty = false;
  int m_generation = 0;
  int m_planning = 0;
  int m_updateCount = 0;
  QString m_error, m_notice;
  std::function<QCoro::Task<void>()> m_retry;
  QVariantMap m_detail;
  std::optional<Detail> m_detailData;
  InstalledList m_installed;
  std::optional<std::pair<QString, QString>> m_current;
  std::vector<std::tuple<QString, QString, QString>> m_trail;
  std::optional<workshop::DownloadPlan> m_pending;
  QSet<QString> m_preparing, m_packIds, m_installedIds, m_activeIds,
      m_knownTags;
  QHash<QString, QSet<QString>> m_collectionMods;
  QHash<QString, workshop::CatalogEntry> m_items;
  Event<bool>::Id m_busyId = 0;
  Event<std::monostate>::Id m_queueId = 0, m_installedId = 0;
  Event<QString>::Id m_urlId = 0;
  Event<workshop::FavouritesState>::Id m_favouritesId = 0;
  Event<QStringList>::Id m_activeId = 0;
};

template <class T>
QCoro::Task<void> WorkshopViewPanel::runCached(
    QString key,
    std::function<QCoro::Task<workshop::CatalogResult<T>>()> produce,
    std::function<void(const T&)> apply, bool cached) {
  if (m_tornDown) co_return;
  QPointer<WorkshopViewPanel> self(this);
  const int generation = ++m_generation;
  m_busy = true;
  m_error.clear();
  m_retry = [self, key, produce, apply, cached] {
    return self->runCached<T>(key, produce, apply, cached);
  };
  emit changed();
  bool shown = false;
  auto show = [self, generation, apply, &shown](const T& value) {
    if (!self || self->m_tornDown || generation != self->m_generation) return;
    apply(value);
    shown = true;
    self->m_busy = false;
    emit self->changed();
  };
  workshop::CatalogResult<void> result;
  if (cached)
    result = co_await m_catalog.cached<T>(key, std::move(produce), show,
                                          workshop::kFreshSeconds, true);
  else {
    auto fetched = co_await produce();
    if (fetched)
      show(*fetched);
    else
      result = std::unexpected(fetched.error());
  }
  if (!self || self->m_tornDown || generation != self->m_generation) co_return;
  if (!result && shown)
    spdlog::warn("[workshop] background refresh failed: {}",
                 result.error().message.toStdString());
  else if (!result)
    self->m_error = result.error().message;
  self->m_busy = false;
  emit self->changed();
  if (self->m_installedDirty && self->m_tab == QStringLiteral("Installed") &&
      !self->m_detailData)
    co_await self->load();
}

}  // namespace px::modrim::workshop
