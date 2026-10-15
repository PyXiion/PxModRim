#pragma once

#include <functional>
#include <optional>
#include <tuple>

#include <QCoro/QCoroQmlTask>
#include <QPointer>
#include <QSet>
#include <QVariantMap>

#include "ui/plugins/workshop/details.h"
#include "ui/plugins/workshop/installed.h"
#include "ui/plugins/workshop/models.h"
#include "ui/views/base.h"

namespace px::ui::workshop {

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
    static constexpr RailViewMeta kRailMeta{QLatin1StringView("workshop"), QLatin1StringView("steam"), QLatin1StringView("Workshop")};
    WorkshopViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app);
    WorkshopViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app,
                      core::workshop::WorkshopCatalog& catalog);
    ~WorkshopViewPanel() override;
    void openRoute(const QStringList& path) override;
    Q_INVOKABLE void teardown() override;
    QCoro::Task<void> follow_route(QStringList path);
    QCoro::Task<void> load(bool append = false);
    QCoro::Task<void> select_tab(QString tab);
    QCoro::Task<void> open_item(QString item_id, QString kind, bool record = true);
    QCoro::Task<void> filter_items(QString query, QString version, QString source, QString sort, QString tag);
    QCoro::Task<void> go_back();
    QCoro::Task<void> retry_load();
    QCoro::Task<void> download_item(QString item_id, QString kind);
    QCoro::Task<void> toggle_activation(QString item_id);
    QCoro::Task<void> toggle_collection(bool only = false);
    QCoro::Task<void> update_all();

    Q_INVOKABLE QCoro::QmlTask refresh() { return load(); }
    Q_INVOKABLE void selectTab(const QString& tab);
    Q_INVOKABLE QCoro::QmlTask filter(QString query, QString version, QString source, QString sort, QString tag) {
        return filter_items(std::move(query), std::move(version), std::move(source), std::move(sort), std::move(tag));
    }
    Q_INVOKABLE QCoro::QmlTask loadMore() { return load(true); }
    Q_INVOKABLE QCoro::QmlTask retry() { return retry_load(); }
    Q_INVOKABLE void openItem(const QString& item_id, const QString& kind);
    Q_INVOKABLE QCoro::QmlTask back() { return go_back(); }
    Q_INVOKABLE QCoro::QmlTask downloadItem(QString item_id, QString kind) { return download_item(std::move(item_id), std::move(kind)); }
    Q_INVOKABLE QCoro::QmlTask toggleActivation(QString item_id) { return toggle_activation(std::move(item_id)); }
    Q_INVOKABLE QCoro::QmlTask toggleCollection() { return toggle_collection(); }
    Q_INVOKABLE QCoro::QmlTask activateOnlyCollection() { return toggle_collection(true); }
    Q_INVOKABLE QCoro::QmlTask updateAll() { return update_all(); }
    Q_INVOKABLE void downloadAvailable();
    Q_INVOKABLE void dismissPlan();
    Q_INVOKABLE void openLink(const QString& url);
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void toggleFavourite(const QString& item_id);

    QString tab() const { return tab_; }
    QString url() const;
    QString backLabel() const;
    bool configured() const { return catalog_.configured(); }
    bool busy() const { return busy_; }
    QString error() const { return error_; }
    QString notice() const { return notice_; }
    QVariantMap detail() const { return detail_; }
    bool hasDetail() const { return !detail_.isEmpty(); }
    bool pendingPlan() const { return pending_.has_value(); }
    bool canDownloadAvailable() const { return pending_ && !pending_->to_download.isEmpty(); }
    QString searchQuery() const { return criteria_.query; }
    QStringList tagOptions() const;
    QString gameVersion() const;
    QStringList versionOptions() const;
    int updateCount() const { return update_count_; }
    bool planning() const { return planning_ > 0; }

    CatalogListModel* mods_model;
    CatalogListModel* collections_model;
    CatalogListModel* members_model;

signals:
    void changed();

private:
    template<class T>
    QCoro::Task<void> run_cached(QString key,
        std::function<QCoro::Task<core::workshop::CatalogResult<T>>()> produce,
        std::function<void(const T&)> apply, bool cached = true);
    QCoro::Task<void> load_pack();
    QCoro::Task<void> download(QStringList ids, QStringList collections);
    QCoro::Task<void> installed_changed();
    QCoro::Task<void> url_changed();
    void refresh_mod_ids();
    QVariantMap row(const core::workshop::CatalogEntry& item, bool detail = false);
    QVariantMap detail_row(const Detail& detail);
    template<class T> QList<QVariantMap> rows(const std::vector<T>& items) {
        refresh_mod_ids();
        QList<QVariantMap> result;
        result.reserve(static_cast<qsizetype>(items.size()));
        for (const auto& item : items) result.append(row(item));
        return result;
    }
    void refresh_rows();
    void clear_detail();
    std::optional<QStringList> pack_copies() const;
    void show_favourites();
    void show_installed();
    void recount();
    std::optional<QString> installed_cursor() const;
    void queue_plan(const core::workshop::DownloadPlan& plan);
    void go(const QStringList& path);

    core::workshop::WorkshopCatalog& catalog_;
    core::DownloadManager& downloads_;
    core::workshop::FavouriteCollections& favourites_;
    QString tab_ = QStringLiteral("Discover");
    core::workshop::CatalogQuery criteria_;
    bool busy_ = false;
    bool torn_down_ = false;
    bool installed_dirty_ = false;
    int generation_ = 0;
    int planning_ = 0;
    int update_count_ = 0;
    QString error_, notice_;
    std::function<QCoro::Task<void>()> retry_;
    QVariantMap detail_;
    std::optional<Detail> detail_data_;
    InstalledList installed_;
    std::optional<std::pair<QString, QString>> current_;
    std::vector<std::tuple<QString, QString, QString>> trail_;
    std::optional<core::workshop::DownloadPlan> pending_;
    QSet<QString> preparing_, pack_ids_, installed_ids_, active_ids_, known_tags_;
    QHash<QString, QSet<QString>> collection_mods_;
    QHash<QString, core::workshop::CatalogEntry> items_;
    core::Event<bool>::Id busy_id_ = 0;
    core::Event<std::monostate>::Id queue_id_ = 0, installed_id_ = 0;
    core::Event<QString>::Id url_id_ = 0;
    core::Event<core::workshop::FavouritesState>::Id favourites_id_ = 0;
    core::Event<QStringList>::Id active_id_ = 0;
};

template<class T>
QCoro::Task<void> WorkshopViewPanel::run_cached(QString key,
    std::function<QCoro::Task<core::workshop::CatalogResult<T>>()> produce,
    std::function<void(const T&)> apply, bool cached) {
    if (torn_down_) co_return;
    QPointer<WorkshopViewPanel> self(this);
    const int generation = ++generation_;
    busy_ = true;
    error_.clear();
    retry_ = [self, key, produce, apply, cached] {
        return self->run_cached<T>(key, produce, apply, cached);
    };
    emit changed();
    bool shown = false;
    auto show = [self, generation, apply, &shown](const T& value) {
        if (!self || self->torn_down_ || generation != self->generation_) return;
        apply(value);
        shown = true;
        self->busy_ = false;
        emit self->changed();
    };
    core::workshop::CatalogResult<void> result;
    if (cached) result = co_await catalog_.cached<T>(key, std::move(produce), show, core::workshop::kFreshSeconds, true);
    else {
        auto fetched = co_await produce();
        if (fetched) show(*fetched);
        else result = std::unexpected(fetched.error());
    }
    if (!self || self->torn_down_ || generation != self->generation_) co_return;
    if (!result && shown) spdlog::warn("[workshop] background refresh failed: {}", result.error().message.toStdString());
    else if (!result) self->error_ = result.error().message;
    self->busy_ = false;
    emit self->changed();
    if (self->installed_dirty_ && self->tab_ == QStringLiteral("Installed") && !self->detail_data_)
        co_await self->load();
}

}  // namespace px::ui::workshop
