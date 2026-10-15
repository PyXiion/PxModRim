#include "core/organizer/service.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

#include <QRegularExpression>
#include <spdlog/spdlog.h>

#include "core/organizer/defaults.h"

namespace px::core::organizer {
namespace {

const QHash<QString, ListedModPtr> kEmptyMods;
const QStringList kEmptyActive;

struct ContextSeam {
    std::function<const QHash<QString, ListedModPtr>&(const CoreContext&)> all_mods =
        [](const CoreContext&) -> const QHash<QString, ListedModPtr>& { return kEmptyMods; };
    std::function<const QStringList&(const CoreContext&)> active_uuids =
        [](const CoreContext&) -> const QStringList& { return kEmptyActive; };
};

ContextSeam& global_context_seam() {
    static ContextSeam seam;
    return seam;
}

Result<QString> clean_name(const QString& name, const QString& what) {
    const QString cleaned = name.trimmed();
    if (cleaned.isEmpty()) {
        return std::unexpected(Error{QStringLiteral("%1 name cannot be empty.").arg(what)});
    }
    return cleaned;
}

}  // namespace

void register_core_context_seam(
    std::function<const QHash<QString, ListedModPtr>&(const CoreContext&)> all_mods,
    std::function<const QStringList&(const CoreContext&)> active_uuids) {
    global_context_seam().all_mods = std::move(all_mods);
    global_context_seam().active_uuids = std::move(active_uuids);
}

OrganizerService::OrganizerService(CoreContext& ctx, OrganizerDb& db)
    : Plugin(QStringLiteral("organizer")), ctx_(&ctx), db_(db) {}

OrganizerService::OrganizerService(AllModsFn all_mods, ActiveUuidsFn active_uuids, OrganizerDb& db)
    : Plugin(QStringLiteral("organizer")),
      all_mods_fn_(std::move(all_mods)),
      active_uuids_fn_(std::move(active_uuids)),
      db_(db) {}

const QHash<QString, ListedModPtr>& OrganizerService::all_mods() const {
    if (all_mods_fn_) {
        return all_mods_fn_();
    }
    if (ctx_) {
        return global_context_seam().all_mods(*ctx_);
    }
    return kEmptyMods;
}

const QStringList& OrganizerService::active_uuids() const {
    if (active_uuids_fn_) {
        return active_uuids_fn_();
    }
    if (ctx_) {
        return global_context_seam().active_uuids(*ctx_);
    }
    return kEmptyActive;
}

QCoro::Task<void> OrganizerService::init() {
    auto loaded = co_await db_.load();
    if (!loaded) {
        // Plugin::init cannot report failure, so the service stays not ready and says why.
        spdlog::error("Cannot load organizer state: {}", loaded.error().message.toStdString());
        co_return;
    }
    state_ = std::move(*loaded);
    changed.notify({});
}

QCoro::Task<void> OrganizerService::init(CoreContext&) {
    co_await init();
}

QCoro::Task<void> OrganizerService::shutdown() {
    co_await db_.close();
}

bool OrganizerService::ready() const noexcept {
    return state_.has_value();
}

const OrganizerState& OrganizerService::state() const {
    Q_ASSERT(state_.has_value());
    return *state_;
}

QCoro::Task<Result<void>> OrganizerService::commit() {
    auto loaded = co_await db_.load();
    if (!loaded) {
        co_return std::unexpected(loaded.error());
    }
    state_ = std::move(*loaded);
    changed.notify({});
    co_return {};
}

// ── Validation helpers ───────────────────────────────────────────────────────

Result<Folder> OrganizerService::find_folder(qint64 folder_id) const {
    const auto& folders = state().folders;
    const auto it = folders.constFind(folder_id);
    if (it == folders.cend()) {
        return std::unexpected(Error{QStringLiteral("That folder no longer exists.")});
    }
    return it.value();
}

Result<Folder> OrganizerService::find_non_root(qint64 folder_id) const {
    if (folder_id == ROOT_ID) {
        return std::unexpected(Error{QStringLiteral("The root folder cannot be changed.")});
    }
    return find_folder(folder_id);
}

Result<Tag> OrganizerService::find_tag(qint64 tag_id) const {
    const auto& tags = state().tags;
    const auto it = tags.constFind(tag_id);
    if (it == tags.cend()) {
        return std::unexpected(Error{QStringLiteral("That tag no longer exists.")});
    }
    return it.value();
}

int OrganizerService::depth(qint64 folder_id) const {
    const auto& folders = state().folders;
    int count = 0;
    Folder current = folders.value(folder_id);
    while (current.parent_id) {
        ++count;
        current = folders.value(*current.parent_id);
    }
    return count;
}

QList<Folder> OrganizerService::children(qint64 folder_id) const {
    QList<Folder> result;
    for (const Folder& folder : state().folders) {
        if (folder.parent_id == folder_id) {
            result.append(folder);
        }
    }
    return result;
}

QSet<qint64> OrganizerService::subtree_ids(qint64 folder_id) const {
    QSet<qint64> ids{folder_id};
    QList<qint64> pending{folder_id};
    while (!pending.isEmpty()) {
        const qint64 current = pending.takeLast();
        for (const Folder& child : children(current)) {
            if (!ids.contains(child.id)) {
                ids.insert(child.id);
                pending.append(child.id);
            }
        }
    }
    return ids;
}

int OrganizerService::height(qint64 folder_id) const {
    int tallest = 0;
    for (const Folder& child : children(folder_id)) {
        tallest = std::max(tallest, height(child.id));
    }
    return 1 + tallest;
}

Result<void> OrganizerService::check_depth(qint64 parent_id, int tree_height) const {
    if (depth(parent_id) + tree_height > MAX_DEPTH) {
        return std::unexpected(
            Error{QStringLiteral("Folders can be nested at most %1 levels deep.").arg(MAX_DEPTH)});
    }
    return {};
}

Result<void> OrganizerService::check_unique_folder_name(qint64 parent_id, const QString& name,
                                                        std::optional<qint64> exclude) const {
    const QString key = name.toCaseFolded();
    for (const Folder& sibling : children(parent_id)) {
        if (exclude && sibling.id == *exclude) {
            continue;
        }
        if (sibling.name.toCaseFolded() == key) {
            return std::unexpected(Error{QStringLiteral("A folder named \"%1\" already exists here.").arg(name)});
        }
    }
    return {};
}

Result<QString> OrganizerService::check_tag(const QString& name, const QString& color,
                                            std::optional<qint64> exclude) const {
    auto cleaned = clean_name(name, QStringLiteral("Tag"));
    if (!cleaned) {
        return std::unexpected(cleaned.error());
    }
    const QString key = cleaned->toCaseFolded();
    for (const Tag& tag : state().tags) {
        if (exclude && tag.id == *exclude) {
            continue;
        }
        if (tag.name.toCaseFolded() == key) {
            return std::unexpected(Error{QStringLiteral("A tag named \"%1\" already exists.").arg(*cleaned)});
        }
    }
    static const QRegularExpression color_pattern(QStringLiteral(R"(\A#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})\z)"));
    if (!color_pattern.match(color).hasMatch()) {
        return std::unexpected(Error{QStringLiteral("\"%1\" is not a valid color (use #RRGGBB).").arg(color)});
    }
    return *cleaned;
}

Result<QString> OrganizerService::validate_new_folder(const QString& name, qint64 parent_id) const {
    auto cleaned = clean_name(name, QStringLiteral("Folder"));
    if (!cleaned) {
        return std::unexpected(cleaned.error());
    }
    if (auto parent = find_folder(parent_id); !parent) {
        return std::unexpected(parent.error());
    }
    if (auto fits = check_depth(parent_id, 1); !fits) {
        return std::unexpected(fits.error());
    }
    if (auto unique = check_unique_folder_name(parent_id, *cleaned); !unique) {
        return std::unexpected(unique.error());
    }
    return *cleaned;
}

QSet<QString> OrganizerService::active_set() const {
    const QStringList& active = active_uuids();
    return QSet<QString>(active.cbegin(), active.cend());
}

// ── Folders ──────────────────────────────────────────────────────────────────

QCoro::Task<Result<Folder>> OrganizerService::create_folder(QString name, qint64 parent_id) {
    auto cleaned = validate_new_folder(name, parent_id);
    if (!cleaned) {
        co_return std::unexpected(cleaned.error());
    }
    auto created = co_await db_.create_folder(parent_id, *cleaned);
    if (!created) {
        co_return std::unexpected(created.error());
    }
    if (auto committed = co_await commit(); !committed) {
        co_return std::unexpected(committed.error());
    }
    co_return *created;
}

QCoro::Task<Result<Folder>> OrganizerService::create_folder_from(QString name, QStringList package_ids,
                                                                 qint64 parent_id) {
    auto cleaned = validate_new_folder(name, parent_id);
    if (!cleaned) {
        co_return std::unexpected(cleaned.error());
    }
    auto created = co_await db_.create_folder_with(parent_id, *cleaned, clean_package_ids(package_ids));
    if (!created) {
        co_return std::unexpected(created.error());
    }
    if (auto committed = co_await commit(); !committed) {
        co_return std::unexpected(committed.error());
    }
    co_return *created;
}

QCoro::Task<Result<void>> OrganizerService::rename_folder(qint64 folder_id, QString name) {
    auto existing = find_non_root(folder_id);
    if (!existing) {
        co_return std::unexpected(existing.error());
    }
    auto cleaned = clean_name(name, QStringLiteral("Folder"));
    if (!cleaned) {
        co_return std::unexpected(cleaned.error());
    }
    if (*cleaned == existing->name) {
        co_return {};
    }
    const qint64 parent_id = existing->parent_id.value_or(ROOT_ID);
    if (auto unique = check_unique_folder_name(parent_id, *cleaned, folder_id); !unique) {
        co_return std::unexpected(unique.error());
    }
    if (auto renamed = co_await db_.rename_folder(folder_id, *cleaned); !renamed) {
        co_return std::unexpected(renamed.error());
    }
    co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::move_folder(qint64 folder_id, qint64 parent_id) {
    auto folder = find_non_root(folder_id);
    if (!folder) {
        co_return std::unexpected(folder.error());
    }
    if (auto target = find_folder(parent_id); !target) {
        co_return std::unexpected(target.error());
    }
    if (folder->parent_id == parent_id) {
        co_return {};
    }
    if (subtree_ids(folder_id).contains(parent_id)) {
        co_return std::unexpected(Error{QStringLiteral("A folder cannot be moved into itself.")});
    }
    // The whole subtree moves, so its height counts against the depth limit.
    if (auto fits = check_depth(parent_id, height(folder_id)); !fits) {
        co_return std::unexpected(fits.error());
    }
    if (auto unique = check_unique_folder_name(parent_id, folder->name, folder_id); !unique) {
        co_return std::unexpected(unique.error());
    }
    if (auto moved = co_await db_.set_parent(folder_id, parent_id); !moved) {
        co_return std::unexpected(moved.error());
    }
    co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::set_collapsed(qint64 folder_id, bool collapsed) {
    auto folder = find_folder(folder_id);
    if (!folder) {
        co_return std::unexpected(folder.error());
    }
    if (folder->collapsed == collapsed) {
        co_return {};
    }
    if (auto updated = co_await db_.set_collapsed(folder_id, collapsed); !updated) {
        co_return std::unexpected(updated.error());
    }
    co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::set_all_collapsed(bool collapsed) {
    const bool unchanged = std::all_of(state().folders.cbegin(), state().folders.cend(),
                                       [collapsed](const Folder& folder) { return folder.collapsed == collapsed; });
    if (unchanged) {
        co_return {};
    }
    if (auto updated = co_await db_.set_all_collapsed(collapsed); !updated) {
        co_return std::unexpected(updated.error());
    }
    co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::delete_folder(qint64 folder_id) {
    if (auto folder = find_non_root(folder_id); !folder) {
        co_return std::unexpected(folder.error());
    }
    if (auto deleted = co_await db_.delete_folder(folder_id); !deleted) {
        co_return std::unexpected(deleted.error());
    }
    co_return co_await commit();
}

// ── Placement ────────────────────────────────────────────────────────────────

QCoro::Task<Result<void>> OrganizerService::place(QStringList package_ids, qint64 folder_id) {
    if (auto folder = find_folder(folder_id); !folder) {
        co_return std::unexpected(folder.error());
    }
    const QStringList pids = clean_package_ids(package_ids);
    if (pids.isEmpty()) {
        co_return {};
    }
    if (auto placed = co_await db_.place(pids, folder_id); !placed) {
        co_return std::unexpected(placed.error());
    }
    co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::ungroup(QStringList package_ids) {
    co_return co_await place(std::move(package_ids), ROOT_ID);
}

QCoro::Task<Result<void>> OrganizerService::reset_to_rules(QStringList package_ids) {
    const QStringList pids = clean_package_ids(package_ids);
    if (pids.isEmpty()) {
        co_return {};
    }
    if (auto unplaced = co_await db_.unplace(pids); !unplaced) {
        co_return std::unexpected(unplaced.error());
    }
    co_return co_await commit();
}

// ── Tags ─────────────────────────────────────────────────────────────────────

QCoro::Task<Result<Tag>> OrganizerService::create_tag(QString name, QString color) {
    auto cleaned = check_tag(name, color);
    if (!cleaned) {
        co_return std::unexpected(cleaned.error());
    }
    auto created = co_await db_.create_tag(*cleaned, color);
    if (!created) {
        co_return std::unexpected(created.error());
    }
    if (auto committed = co_await commit(); !committed) {
        co_return std::unexpected(committed.error());
    }
    co_return *created;
}

QCoro::Task<Result<void>> OrganizerService::update_tag(qint64 tag_id, QString name, QString color) {
    if (auto tag = find_tag(tag_id); !tag) {
        co_return std::unexpected(tag.error());
    }
    auto cleaned = check_tag(name, color, tag_id);
    if (!cleaned) {
        co_return std::unexpected(cleaned.error());
    }
    if (auto updated = co_await db_.update_tag(tag_id, *cleaned, color); !updated) {
        co_return std::unexpected(updated.error());
    }
    co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::delete_tag(qint64 tag_id) {
    if (auto tag = find_tag(tag_id); !tag) {
        co_return std::unexpected(tag.error());
    }
    if (auto deleted = co_await db_.delete_tag(tag_id); !deleted) {
        co_return std::unexpected(deleted.error());
    }
    co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::set_mod_tags(QStringList package_ids, QSet<qint64> add,
                                                         QSet<qint64> remove) {
    const QStringList pids = clean_package_ids(package_ids);
    if (add.intersects(remove)) {
        co_return std::unexpected(Error{QStringLiteral("A tag cannot be added and removed at once.")});
    }
    for (qint64 tag_id : add | remove) {
        if (auto tag = find_tag(tag_id); !tag) {
            co_return std::unexpected(tag.error());
        }
    }
    if (pids.isEmpty() || (add.isEmpty() && remove.isEmpty())) {
        co_return {};
    }
    if (auto updated = co_await db_.set_mod_tags(pids, add, remove); !updated) {
        co_return std::unexpected(updated.error());
    }
    co_return co_await commit();
}

// ── Rules ────────────────────────────────────────────────────────────────────

QCoro::Task<Result<void>> OrganizerService::set_rules(QList<RuleSpec> specs) {
    QList<RuleSpec> cleaned;
    cleaned.reserve(specs.size());
    for (RuleSpec spec : specs) {
        spec.pattern = spec.pattern.trimmed();
        if (spec.pattern.isEmpty()) {
            co_return std::unexpected(Error{QStringLiteral("Rule pattern cannot be empty.")});
        }
        if (spec.folder_id == ROOT_ID) {
            co_return std::unexpected(Error{QStringLiteral("Rules must target a folder, not the root.")});
        }
        if (auto target = find_folder(spec.folder_id); !target) {
            co_return std::unexpected(target.error());
        }
        cleaned.append(std::move(spec));
    }
    if (auto replaced = co_await db_.replace_rules(cleaned); !replaced) {
        co_return std::unexpected(replaced.error());
    }
    co_return co_await commit();
}

QCoro::Task<Result<int>> OrganizerService::add_standard_rules() {
    // (field, op, casefolded pattern) identifies a rule regardless of folder or position.
    using RuleKey = std::tuple<int, int, QString>;
    std::set<RuleKey> present;
    for (const Rule& rule : state().rules) {
        present.emplace(static_cast<int>(rule.field), static_cast<int>(rule.op), rule.pattern.toCaseFolded());
    }

    QSet<QString> existing;
    for (const Folder& folder : children(ROOT_ID)) {
        existing.insert(folder.name.toCaseFolded());
    }

    QList<StandardRule> rules;
    QStringList new_folders;
    QSet<QString> new_folder_keys;
    for (const StandardRule& rule : standard_rules()) {
        const RuleKey key{static_cast<int>(rule.field), static_cast<int>(rule.op), rule.pattern.toCaseFolded()};
        if (present.contains(key)) {
            continue;
        }
        present.insert(key);
        rules.append(rule);
        // The first spelling of a missing top-level folder names it, as in the legacy service.
        const QString folder_key = rule.folder.toCaseFolded();
        if (!existing.contains(folder_key) && !new_folder_keys.contains(folder_key)) {
            new_folder_keys.insert(folder_key);
            new_folders.append(rule.folder);
        }
    }
    if (rules.isEmpty()) {
        co_return 0;
    }
    if (auto appended = co_await db_.append_named_rules(ROOT_ID, new_folders, rules); !appended) {
        co_return std::unexpected(appended.error());
    }
    if (auto committed = co_await commit(); !committed) {
        co_return std::unexpected(committed.error());
    }
    co_return static_cast<int>(rules.size());
}

// ── Read helpers ─────────────────────────────────────────────────────────────

FolderNode OrganizerService::tree(const std::optional<TreeQuery>& query) const {
    return build_tree(state(), all_mods(), active_set(), query);
}

Result<QStringList> OrganizerService::folder_mod_uuids(qint64 folder_id, bool recursive) const {
    if (auto folder = find_folder(folder_id); !folder) {
        return std::unexpected(folder.error());
    }
    const FolderNode root = tree();
    for (const FolderNode* node : root.walk()) {
        if (node->folder.id != folder_id) {
            continue;
        }
        const QList<ModLeaf> leaves = recursive ? node->all_mods() : node->mods;
        QStringList uuids;
        for (const ModLeaf& leaf : leaves) {
            uuids.append(leaf.uuid);
        }
        return uuids;
    }
    return QStringList{};
}

Result<FolderToggle> OrganizerService::folder_toggle(qint64 folder_id, bool recursive) const {
    if (auto folder = find_folder(folder_id); !folder) {
        return std::unexpected(folder.error());
    }
    const FolderNode root = tree();
    for (const FolderNode* node : root.walk()) {
        if (node->folder.id != folder_id) {
            continue;
        }
        const QList<ModLeaf> leaves = recursive ? node->all_mods() : node->mods;
        QStringList uuids;
        for (const ModLeaf& leaf : leaves) {
            uuids.append(leaf.uuid);
        }
        const CheckState check = recursive ? node->check : node->own_check();
        if (check == CheckState::On) {
            return FolderToggle{QStringList{}, uuids};
        }
        return FolderToggle{uuids, QStringList{}};
    }
    return FolderToggle{};
}

Result<QSet<qint64>> OrganizerService::folder_move_targets(qint64 folder_id) const {
    auto folder = find_non_root(folder_id);
    if (!folder) {
        return std::unexpected(folder.error());
    }
    const QSet<qint64> subtree = subtree_ids(folder_id);
    const int tree_height = height(folder_id);
    const QString key = folder->name.toCaseFolded();

    QSet<qint64> targets;
    for (const Folder& candidate : state().folders) {
        if (subtree.contains(candidate.id) || candidate.id == folder->parent_id) {
            continue;
        }
        if (depth(candidate.id) + tree_height > MAX_DEPTH) {
            continue;
        }
        const QList<Folder> siblings = children(candidate.id);
        const bool clash = std::any_of(siblings.cbegin(), siblings.cend(),
                                       [&key](const Folder& child) { return child.name.toCaseFolded() == key; });
        if (!clash) {
            targets.insert(candidate.id);
        }
    }
    return targets;
}

QList<TreeFilter> OrganizerService::tree_filters() const {
    return organizer::tree_filters(all_mods(), active_set(), &state());
}

}  // namespace px::core::organizer
