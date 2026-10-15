#include "ui/plugins/workshop/view.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDesktopServices>
#include <QQmlContext>
#include <QQuickWidget>
#include <QUrl>
#include <spdlog/spdlog.h>

#include "ui/components/mod_activation.h"
#include "ui/navigation.h"
#include "ui/theme/palette.h"

namespace px::ui::workshop {
using namespace core::workshop;
namespace {
QString id_of(const CatalogEntry& item) {
    return std::visit([](const auto& value) { return QString::fromStdString(value.id); }, item);
}
QSet<QString> ids_of(const std::vector<std::string>& ids) {
    QSet<QString> result;
    for (const auto& id : ids) result.insert(QString::fromStdString(id));
    return result;
}
std::optional<QString> cursor_of(const std::optional<std::string>& cursor) {
    return cursor ? std::optional(QString::fromStdString(*cursor)) : std::nullopt;
}
WorkshopCatalog& catalog_of(core::CoreContext& ctx) {
    return *static_cast<WorkshopCatalog*>(ctx.plugins().get(QStringLiteral("workshop_catalog")));
}
}

WorkshopViewPanel::WorkshopViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : WorkshopViewPanel(ctx, engine, parent, app, catalog_of(ctx)) {}

WorkshopViewPanel::WorkshopViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app,
                                   WorkshopCatalog& catalog)
    : BaseViewPanel(ctx, engine, parent, app), mods_model(new CatalogListModel(this)),
      collections_model(new CatalogListModel(this)), members_model(new CatalogListModel(this)),
      catalog_(catalog), downloads_(ctx.downloads()), favourites_(catalog.favourites()) {
    if (!gameVersion().isEmpty()) criteria_.version = gameVersion();
    refresh_mod_ids();
    qml_ = new QQuickWidget(engine, this);
    qml_->setObjectName(QStringLiteral("workshopView"));
    qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml_->setClearColor(px::ui::paletteColor(u"ELEVATE_0"));
    qml_->rootContext()->setContextProperty(QStringLiteral("workshopPanel"), this);
    qml_->rootContext()->setContextProperty(QStringLiteral("catalogMods"), mods_model);
    qml_->rootContext()->setContextProperty(QStringLiteral("catalogCollections"), collections_model);
    qml_->rootContext()->setContextProperty(QStringLiteral("catalogMembers"), members_model);
    busy_id_ = downloads_.busy_changed.connect([this](bool) { refresh_rows(); emit changed(); });
    queue_id_ = catalog_.queue_changed.connect([this](const std::monostate&) { refresh_rows(); emit changed(); });
    url_id_ = catalog_.catalog_url_changed.connect([this](const QString&) { url_changed(); });
    installed_id_ = catalog_.installed_changed.connect([this](const std::monostate&) { installed_changed(); });
    favourites_id_ = favourites_.changed().connect([this](const FavouritesState&) {
        if (tab_ == QStringLiteral("Favourites") && !detail_data_) show_favourites();
        else { refresh_rows(); emit changed(); }
    });
    active_id_ = ctx_.activeStateChanged().connect([this](const QStringList&) {
        refresh_mod_ids(); refresh_rows(); emit changed();
    });
    qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/Workshop.qml")));
    root_->addWidget(qml_, 1);
}

WorkshopViewPanel::~WorkshopViewPanel() {
    teardown();
    // Workshop.qml bindings read workshopPanel; the view must die while this panel is still alive.
    delete qml_;
}
void WorkshopViewPanel::teardown() {
    if (torn_down_) return;
    torn_down_ = true;
    ++generation_;
    retry_ = {};
    downloads_.busy_changed.disconnect(busy_id_);
    catalog_.queue_changed.disconnect(queue_id_);
    catalog_.catalog_url_changed.disconnect(url_id_);
    catalog_.installed_changed.disconnect(installed_id_);
    favourites_.changed().disconnect(favourites_id_);
    ctx_.activeStateChanged().disconnect(active_id_);
}

QString WorkshopViewPanel::gameVersion() const {
    const auto version = ctx_.targetVersion();
    return version == QStringLiteral("Unknown") ? QString{} : version;
}
void WorkshopViewPanel::refresh_mod_ids() {
    installed_ids_.clear(); active_ids_.clear();
    const auto& mods = ctx_.allMods();
    for (const auto& mod : mods) if (auto id = mod->published_file_id()) installed_ids_.insert(*id);
    for (const auto& uuid : ctx_.activeUuids()) {
        auto mod = mods.value(uuid);
        if (mod) if (auto id = mod->published_file_id()) active_ids_.insert(*id);
    }
}

QVariantMap WorkshopViewPanel::row(const CatalogEntry& item, bool detail) {
    const auto id = id_of(item);
    items_.insert(id, item);
    std::visit([this](const auto& value) {
        auto tags = ids_of(value.tags);
        tags.subtract(ids_of(value.supported_versions));
        known_tags_.unite(tags);
    }, item);
    auto result = item_row(item, [this](const CatalogMod& mod) { return catalog_.install_state(mod); }, gameVersion(), detail);
    const auto* mod = std::get_if<CatalogMod>(&item);
    const auto* collection = std::get_if<CatalogCollection>(&item);
    result[QStringLiteral("active")] = mod && active_ids_.contains(id);
    if (collection) {
        if (!detail) {
            qsizetype count = 0;
            for (const auto& member : collection->member_ids) count += installed_ids_.contains(QString::fromStdString(member));
            if (!collection->member_ids.empty() && count == static_cast<qsizetype>(collection->member_ids.size())) {
                result[QStringLiteral("state")] = QStringLiteral("installed");
                result[QStringLiteral("stateLabel")] = QStringLiteral("Installed");
                result[QStringLiteral("actionLabel")] = QStringLiteral("Installed");
            } else if (count) result[QStringLiteral("actionLabel")] = QStringLiteral("Complete download");
        }
        if (favourites_.contains(collection->id)) {
            result[QStringLiteral("favourite")] = true;
            favourites_.remember(*collection);
        }
    }
    if (collection || result.value(QStringLiteral("state")).toString() != QStringLiteral("installed")) {
        if (preparing_.contains(id)) {
            result[QStringLiteral("queued")] = true;
            result[QStringLiteral("actionLabel")] = QStringLiteral("Preparing…");
        } else {
            QSet<QString> ids = mod ? QSet<QString>{id} : ids_of(collection->member_ids) | collection_mods_.value(id);
            if (collection && detail_data_ && id_of(detail_data_->item) == id) ids.unite(pack_ids_);
            if (ids.intersects(downloads_.active_ids())) {
                result[QStringLiteral("queued")] = true;
                result[QStringLiteral("actionLabel")] = QStringLiteral("Downloading");
            } else if (ids.intersects(catalog_.queued_ids())) {
                result[QStringLiteral("queued")] = true;
                result[QStringLiteral("actionLabel")] = QStringLiteral("Queued");
            }
        }
    }
    return result;
}

QVariantMap WorkshopViewPanel::detail_row(const Detail& detail) {
    auto result = row(detail.item, true);
    result[QStringLiteral("complete")] = detail.complete;
    result[QStringLiteral("warning")] = QString::fromStdString(detail.warning);
    if (std::holds_alternative<CatalogCollection>(detail.item)) {
        auto collage = result.value(QStringLiteral("collage")).toStringList();
        if (result.value(QStringLiteral("previewUrl")).toString().isEmpty() && collage.isEmpty()) {
            for (const auto& member : detail.members) {
                const auto* mod = std::get_if<CatalogMod>(&member);
                if (!mod) continue;
                QString preview = QString::fromStdString(mod->preview_url.value_or(""));
                if (preview.isEmpty()) for (const auto& image : mod->previews) {
                    if (image.type == PreviewType::image) { preview = QString::fromStdString(image.url); break; }
                }
                preview = safe_url(preview);
                if (!preview.isEmpty()) collage.append(preview);
                if (collage.size() == 4) break;
            }
            result[QStringLiteral("collage")] = collage;
        }
        bool started = false;
        for (const auto& member : detail.members) if (auto* mod = std::get_if<CatalogMod>(&member))
            started |= catalog_.install_state(*mod) != InstallState::missing;
        if (!result.value(QStringLiteral("queued")).toBool())
            result[QStringLiteral("actionLabel")] = started ? QStringLiteral("Complete download") : QStringLiteral("Download all");
        if (auto copies = pack_copies()) {
            result[QStringLiteral("state")] = QStringLiteral("installed");
            result[QStringLiteral("stateLabel")] = QStringLiteral("Installed");
            if (!result.value(QStringLiteral("queued")).toBool()) result[QStringLiteral("actionLabel")] = QStringLiteral("Installed");
            result[QStringLiteral("active")] = std::ranges::all_of(*copies, [this](const auto& uuid) { return ctx_.activeUuids().contains(uuid); });
        }
    }
    return result;
}
void WorkshopViewPanel::refresh_rows() {
    for (auto* model : {mods_model, collections_model, members_model}) model->refresh_rows([this](const QVariantMap& value) {
        return row(items_.value(value.value(QStringLiteral("itemId")).toString()));
    });
    if (detail_data_) detail_ = detail_row(*detail_data_);
}
void WorkshopViewPanel::clear_detail() {
    detail_.clear(); detail_data_.reset(); current_.reset(); trail_.clear(); pack_ids_.clear();
}
std::optional<QStringList> WorkshopViewPanel::pack_copies() const {
    if (torn_down_ || pack_ids_.isEmpty()) return std::nullopt;
    QHash<QString, QStringList> installed;
    for (auto it = ctx_.allMods().cbegin(); it != ctx_.allMods().cend(); ++it)
        if (auto id = it.value()->published_file_id(); id && pack_ids_.contains(*id)) installed[*id].append(it.key());
    if (installed.size() < pack_ids_.size()) return std::nullopt;
    QStringList copies;
    for (const auto& uuids : installed) {
        auto active = std::ranges::find_if(uuids, [this](const auto& uuid) { return ctx_.activeUuids().contains(uuid); });
        copies.append(active == uuids.cend() ? uuids.front() : *active);
    }
    return copies;
}
QCoro::Task<void> WorkshopViewPanel::load_pack() {
    if (!detail_data_ || torn_down_) co_return;
    QPointer<WorkshopViewPanel> self(this);
    const int generation = generation_;
    auto result = co_await catalog_.plan({}, {id_of(detail_data_->item)});
    if (!self || self->torn_down_ || generation != self->generation_) co_return;
    if (!result) {
        spdlog::warn("[workshop] cannot resolve collection mods: {}", result.error().message.toStdString());
        co_return;
    }
    self->pack_ids_.clear();
    if (result->is_complete) {
        self->pack_ids_ = QSet<QString>(result->to_download.cbegin(), result->to_download.cend());
        self->pack_ids_.unite(QSet<QString>(result->already_current.cbegin(), result->already_current.cend()));
    }
    self->refresh_rows(); emit self->changed();
}

QCoro::Task<void> WorkshopViewPanel::load(bool append) {
    if (torn_down_) co_return;
    if (!configured()) { ++generation_; busy_ = false; emit changed(); co_return; }
    clear_detail();
    auto* catalog = &catalog_;
    if (tab_ == QStringLiteral("Discover")) {
        co_await run_cached<Discover>(QStringLiteral("discover"), [catalog] { return catalog->discover(); }, [this](const Discover& value) {
            mods_model->set_page(rows(value.mods.items), static_cast<int>(value.mods.total), std::nullopt);
            collections_model->set_page(rows(value.collections.items), static_cast<int>(value.collections.total), std::nullopt);
        });
    } else if (tab_ == QStringLiteral("Favourites")) {
        ++generation_; busy_ = false; error_.clear(); show_favourites();
    } else if (tab_ == QStringLiteral("Installed")) {
        if (append) {
            mods_model->set_page(rows(installed_.next_page()), installed_.total(), installed_cursor(), true);
            co_return;
        }
        installed_dirty_ = false;
        auto ids = installed_ids_.values();
        std::ranges::sort(ids);
        const auto fingerprint = QCryptographicHash::hash(ids.join(QLatin1Char(',')).toUtf8(), QCryptographicHash::Sha256).toHex();
        co_await run_cached<std::vector<CatalogMod>>(QStringLiteral("installed|") + QString::fromLatin1(fingerprint),
            [catalog] { return catalog->installed_with_updates(); }, [this](const std::vector<CatalogMod>& value) {
                installed_.replace(value); recount(); show_installed();
            });
    } else {
        const bool collections = tab_ == QStringLiteral("Collections");
        auto* model = collections ? collections_model : mods_model;
        auto query = criteria_;
        query.cursor = append ? model->cursor() : std::nullopt;
        const QString key = tab_ + QLatin1Char('|') + QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(QJsonArray{query.query, query.tag.value_or(QString{}), query.version.value_or(QString{}),
                query.sort, query.limit, query.source}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        if (collections) {
            co_await run_cached<CatalogPage<CatalogCollection>>(key, [catalog, query] { return catalog->collections(query); },
                [this, model, append](const CatalogPage<CatalogCollection>& value) {
                    model->set_page(rows(value.items), static_cast<int>(value.total), cursor_of(value.next_cursor), append);
                }, !append);
        } else {
            co_await run_cached<CatalogPage<CatalogMod>>(key, [catalog, query] { return catalog->mods(query); },
                [this, model, append](const CatalogPage<CatalogMod>& value) {
                    model->set_page(rows(value.items), static_cast<int>(value.total), cursor_of(value.next_cursor), append);
                }, !append);
        }
    }
}

void WorkshopViewPanel::show_favourites() {
    std::vector<CatalogCollection> matches;
    for (const auto& item : favourites_.items())
        if (QString::fromStdString(item.title).toCaseFolded().contains(criteria_.query.toCaseFolded())) matches.push_back(item);
    collections_model->set_page(rows(matches), static_cast<int>(matches.size()), std::nullopt);
    emit changed();
}
std::optional<QString> WorkshopViewPanel::installed_cursor() const {
    return installed_.has_more() ? std::optional(QStringLiteral("more")) : std::nullopt;
}
void WorkshopViewPanel::show_installed() {
    clear_detail();
    mods_model->set_page(rows(installed_.visible()), installed_.total(), installed_cursor());
    emit changed();
}
void WorkshopViewPanel::recount() {
    update_count_ = 0;
    for (const auto& mod : installed_.mods()) update_count_ += catalog_.install_state(mod) == InstallState::outdated;
}

QCoro::Task<void> WorkshopViewPanel::select_tab(QString tab) {
    tab_ = std::move(tab); notice_.clear(); pending_.reset(); clear_detail(); installed_.reset_paging();
    co_await load();
}
QCoro::Task<void> WorkshopViewPanel::filter_items(QString query, QString version, QString source, QString sort, QString tag) {
    criteria_ = CatalogQuery{.query = query, .tag = tag.isEmpty() ? std::nullopt : std::optional(tag),
        .version = version.isEmpty() ? std::nullopt : std::optional(version), .sort = sort, .source = source};
    installed_.search(query);
    if (tab_ == QStringLiteral("Installed") && installed_.loaded()) { show_installed(); co_return; }
    if (tab_ == QStringLiteral("Discover")) tab_ = QStringLiteral("Mods");
    co_await load();
}
QCoro::Task<void> WorkshopViewPanel::retry_load() {
    if (retry_ && !torn_down_) {
        auto action = retry_;
        co_await action();
    }
    co_return;
}
void WorkshopViewPanel::go(const QStringList& path) { app_.navigate(buildRoute(QStringLiteral("workshop"), path)); }
void WorkshopViewPanel::selectTab(const QString& tab) { go({tab.toLower()}); }
void WorkshopViewPanel::openItem(const QString& item_id, const QString& kind) { go({kind, item_id}); }
void WorkshopViewPanel::openRoute(const QStringList& path) { follow_route(path); }
QCoro::Task<void> WorkshopViewPanel::follow_route(QStringList path) {
    if (torn_down_ || path.isEmpty()) co_return;
    const QStringList tabs{QStringLiteral("Discover"), QStringLiteral("Mods"), QStringLiteral("Collections"), QStringLiteral("Favourites"), QStringLiteral("Installed")};
    if (path.size() == 1) {
        for (const auto& tab : tabs) if (tab.compare(path.front(), Qt::CaseInsensitive) == 0) {
            co_await select_tab(tab); co_return;
        }
    } else if (path.size() == 2 && (path.front() == QStringLiteral("mod") || path.front() == QStringLiteral("collection"))) {
        co_await open_item(path[1], path[0]); co_return;
    }
    spdlog::warn("[workshop] unknown route {}", buildRoute(QStringLiteral("workshop"), path).toStdString());
}
QCoro::Task<void> WorkshopViewPanel::open_item(QString item_id, QString kind, bool record) {
    pack_ids_.clear();
    QPointer<WorkshopViewPanel> self(this);
    auto* catalog = &catalog_;
    co_await run_cached<Detail>(QStringLiteral("detail|") + kind + QLatin1Char('|') + item_id,
        [catalog, item_id, kind] { return fetch_detail(*catalog, item_id, kind); },
        [this, item_id, kind, record](const Detail& value) {
            const auto ref = std::pair(kind, item_id);
            if (current_ != ref) {
                if (detail_.isEmpty()) trail_.clear();
                else if (record && current_) trail_.emplace_back(current_->first, current_->second, detail_.value(QStringLiteral("title")).toString());
            }
            current_ = ref;
            auto members = rows(value.members);
            detail_data_ = value;
            detail_ = detail_row(value);
            members_model->set_page(std::move(members), static_cast<int>(value.members.size()), std::nullopt);
        });
    if (self && !self->torn_down_ && kind == QStringLiteral("collection") && self->current_ == std::pair(kind, item_id))
        co_await self->load_pack();
}
QCoro::Task<void> WorkshopViewPanel::go_back() {
    if (torn_down_) co_return;
    if (!trail_.empty()) {
        auto [kind, item_id, title] = trail_.back();
        QPointer<WorkshopViewPanel> self(this);
        co_await open_item(item_id, kind, false);
        if (self && !self->torn_down_ && self->current_ == std::pair(kind, item_id) && !self->trail_.empty()) {
            self->trail_.pop_back(); emit self->changed();
        }
        co_return;
    }
    ++generation_; clear_detail(); error_.clear(); busy_ = false; emit changed();
    if (tab_ == QStringLiteral("Favourites")) show_favourites();
    else if (tab_ == QStringLiteral("Installed") && installed_dirty_) co_await load();
}

QCoro::Task<void> WorkshopViewPanel::toggle_activation(QString item_id) {
    if (torn_down_) co_return;
    QStringList copies, active;
    for (auto it = ctx_.allMods().cbegin(); it != ctx_.allMods().cend(); ++it) {
        if (it.value()->published_file_id() != item_id) continue;
        copies.append(it.key());
        if (ctx_.activeUuids().contains(it.key())) active.append(it.key());
    }
    if (copies.isEmpty()) co_return;
    if (active.isEmpty()) co_await apply_activation(ctx_, this, {copies.front()}, {});
    else co_await apply_activation(ctx_, this, {}, active);
}
QCoro::Task<void> WorkshopViewPanel::toggle_collection(bool only) {
    auto copies = pack_copies();
    if (!copies) co_return;
    QStringList disable;
    const bool all_active = std::ranges::all_of(*copies, [this](const auto& uuid) { return ctx_.activeUuids().contains(uuid); });
    for (const auto& uuid : ctx_.activeUuids()) {
        auto mod = ctx_.allMods().value(uuid);
        if (!mod) continue;
        auto id = mod->published_file_id();
        if (id && ((only && !copies->contains(uuid)) || (!only && all_active && pack_ids_.contains(*id)))) disable.append(uuid);
    }
    if (!only && all_active) co_await apply_activation(ctx_, this, {}, disable);
    else co_await apply_activation(ctx_, this, *copies, disable);
}
QCoro::Task<void> WorkshopViewPanel::download_item(QString item_id, QString kind) {
    co_await download(kind == QStringLiteral("mod") ? QStringList{item_id} : QStringList{},
                      kind == QStringLiteral("collection") ? QStringList{item_id} : QStringList{});
}
QCoro::Task<void> WorkshopViewPanel::update_all() {
    QStringList ids;
    for (const auto& mod : installed_.mods()) if (catalog_.install_state(mod) == InstallState::outdated) ids.append(QString::fromStdString(mod.id));
    if (!ids.isEmpty()) co_await download(ids, {});
}
void WorkshopViewPanel::queue_plan(const DownloadPlan& plan) { catalog_.enqueue(plan); notice_ = queued_notice(plan.to_download.size()); }
QCoro::Task<void> WorkshopViewPanel::download(QStringList ids, QStringList collections) {
    if (torn_down_) co_return;
    QPointer<WorkshopViewPanel> self(this);
    notice_ = QStringLiteral("Preparing download…"); pending_.reset(); ++planning_;
    auto requested = QSet<QString>(ids.cbegin(), ids.cend());
    requested.unite(QSet<QString>(collections.cbegin(), collections.cend()));
    preparing_.unite(requested); refresh_rows(); emit changed();
    auto result = co_await catalog_.plan(ids, collections);
    if (!self || self->torn_down_) co_return;
    --self->planning_; self->preparing_.subtract(requested); self->refresh_rows(); emit self->changed();
    if (!result) { self->notice_ = QStringLiteral("Could not prepare the download: ") + result.error().message; emit self->changed(); co_return; }
    for (const auto& collection : collections)
        self->collection_mods_[collection] = QSet<QString>(result->to_download.cbegin(), result->to_download.cend());
    if (!result->is_complete) { self->pending_ = *result; self->notice_ = incomplete_plan_notice(*result); }
    else if (result->to_download.isEmpty()) self->notice_ = QStringLiteral("All available mods are already installed and up to date.");
    else self->queue_plan(*result);
    emit self->changed();
}
void WorkshopViewPanel::downloadAvailable() {
    if (torn_down_) return;
    auto plan = std::exchange(pending_, std::nullopt);
    if (plan && !plan->to_download.isEmpty()) queue_plan(*plan);
    emit changed();
}
void WorkshopViewPanel::dismissPlan() { pending_.reset(); notice_.clear(); emit changed(); }
void WorkshopViewPanel::openLink(const QString& url) { if (!safe_url(url).isEmpty()) QDesktopServices::openUrl(QUrl(url)); }
void WorkshopViewPanel::openSettings() { app_.navigate(buildRoute(QString(kSettingsViewId))); }
void WorkshopViewPanel::toggleFavourite(const QString& item_id) {
    const auto found = items_.constFind(item_id);
    if (found != items_.cend()) if (const auto* collection = std::get_if<CatalogCollection>(&*found))
        favourites_.set(*collection, !favourites_.contains(collection->id));
}
QCoro::Task<void> WorkshopViewPanel::url_changed() {
    if (torn_down_) co_return;
    clear_detail(); error_.clear(); notice_.clear(); pending_.reset(); items_.clear(); known_tags_.clear();
    for (auto* model : {mods_model, collections_model, members_model}) model->set_page({}, 0, std::nullopt);
    co_await load();
}
QCoro::Task<void> WorkshopViewPanel::installed_changed() {
    if (torn_down_) co_return;
    installed_dirty_ = true; refresh_mod_ids(); recount(); refresh_rows(); emit changed();
    if (tab_ == QStringLiteral("Installed") && !busy_ && !detail_data_) co_await load();
}
QString WorkshopViewPanel::url() const {
    return buildRoute(QStringLiteral("workshop"), current_ ? QStringList{current_->first, current_->second} : QStringList{tab_.toLower()});
}
QString WorkshopViewPanel::backLabel() const {
    return QStringLiteral("Back to ") + (trail_.empty() ? tab_ : std::get<2>(trail_.back()));
}
QStringList WorkshopViewPanel::tagOptions() const {
    auto tags = known_tags_.values(); std::ranges::sort(tags); tags.prepend(QStringLiteral("All tags")); return tags;
}
QStringList WorkshopViewPanel::versionOptions() const {
    QStringList versions{QStringLiteral("All versions")};
    if (criteria_.version) versions.append(*criteria_.version);
    for (const auto& version : {QStringLiteral("1.6"), QStringLiteral("1.5"), QStringLiteral("1.4"), QStringLiteral("1.3")})
        if (!versions.contains(version)) versions.append(version);
    return versions;
}

}  // namespace px::ui::workshop
