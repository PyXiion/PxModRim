#pragma once

#include <functional>
#include <optional>
#include <variant>
#include <QCoro/QCoroTask>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/context_fwd.h"
#include "core/error.h"
#include "core/events.h"
#include "core/organizer/db.h"
#include "core/organizer/models.h"
#include "core/organizer/resolve.h"
#include "core/plugin.h"

namespace px::modrim {
class CoreContext;
}

namespace px::modrim::organizer {

struct FolderToggle {
    QStringList enable;
    QStringList disable;
};

// Virtual folder tree, tags and auto-folder rules. Never touches load order.
// Reads mods and the active list from the context; each mutation persists, reloads the
// state from the database and then emits `changed`.
class OrganizerService : public Plugin {
public:
    using AllModsFn = std::function<const QHash<QString, ListedModPtr>&()>;
    using ActiveUuidsFn = std::function<const QStringList&()>;

    OrganizerService(CoreContext& ctx, OrganizerDb& db);
    OrganizerService(AllModsFn all_mods, ActiveUuidsFn active_uuids, OrganizerDb& db);

    Event<std::monostate> changed;

    QCoro::Task<void> init();
    QCoro::Task<void> init(CoreContext&) override;
    QCoro::Task<void> shutdown() override;

    // Whether init() has loaded state; views built earlier must wait.
    bool ready() const noexcept;
    // Requires ready(): callers check before reading.
    const OrganizerState& state() const;

    // ── Folders ──────────────────────────────────────────────────────────────
    QCoro::Task<Result<Folder>> create_folder(QString name, qint64 parent_id = ROOT_ID);
    QCoro::Task<Result<Folder>> create_folder_from(QString name, QStringList package_ids,
                                                   qint64 parent_id = ROOT_ID);
    QCoro::Task<Result<void>> rename_folder(qint64 folder_id, QString name);
    QCoro::Task<Result<void>> move_folder(qint64 folder_id, qint64 parent_id);
    QCoro::Task<Result<void>> set_collapsed(qint64 folder_id, bool collapsed);
    QCoro::Task<Result<void>> set_all_collapsed(bool collapsed);
    QCoro::Task<Result<void>> delete_folder(qint64 folder_id);

    // ── Placement ────────────────────────────────────────────────────────────
    QCoro::Task<Result<void>> place(QStringList package_ids, qint64 folder_id);
    QCoro::Task<Result<void>> ungroup(QStringList package_ids);
    QCoro::Task<Result<void>> reset_to_rules(QStringList package_ids);

    // ── Tags ─────────────────────────────────────────────────────────────────
    QCoro::Task<Result<Tag>> create_tag(QString name, QString color);
    QCoro::Task<Result<void>> update_tag(qint64 tag_id, QString name, QString color);
    QCoro::Task<Result<void>> delete_tag(qint64 tag_id);
    QCoro::Task<Result<void>> set_mod_tags(QStringList package_ids, QSet<qint64> add, QSet<qint64> remove);

    // ── Rules ────────────────────────────────────────────────────────────────
    QCoro::Task<Result<void>> set_rules(QList<RuleSpec> specs);
    // Appends the built-in rules not yet present. Returns how many were added.
    QCoro::Task<Result<int>> add_standard_rules();

    // ── Read helpers ─────────────────────────────────────────────────────────
    FolderNode tree(const std::optional<TreeQuery>& query = std::nullopt) const;
    // Mods under `folder_id`; `recursive = false` keeps only its direct mods.
    Result<QStringList> folder_mod_uuids(qint64 folder_id, bool recursive = true) const;
    // (enable, disable) for a folder checkbox click: a fully enabled folder disables
    // everything; partial or off enables all.
    Result<FolderToggle> folder_toggle(qint64 folder_id, bool recursive = true) const;
    // Legal destinations, excluding the folder's current parent.
    Result<QSet<qint64>> folder_move_targets(qint64 folder_id) const;
    QList<TreeFilter> tree_filters() const;

private:
    QCoro::Task<Result<void>> commit();

    Result<QString> validate_new_folder(const QString& name, qint64 parent_id) const;
    Result<Folder> find_folder(qint64 folder_id) const;
    Result<Folder> find_non_root(qint64 folder_id) const;
    Result<Tag> find_tag(qint64 tag_id) const;
    Result<QString> check_tag(const QString& name, const QString& color,
                              std::optional<qint64> exclude = std::nullopt) const;
    Result<void> check_unique_folder_name(qint64 parent_id, const QString& name,
                                          std::optional<qint64> exclude = std::nullopt) const;
    Result<void> check_depth(qint64 parent_id, int height) const;
    int depth(qint64 folder_id) const;
    int height(qint64 folder_id) const;
    QList<Folder> children(qint64 folder_id) const;
    QSet<qint64> subtree_ids(qint64 folder_id) const;

    const QHash<QString, ListedModPtr>& all_mods() const;
    const QStringList& active_uuids() const;
    QSet<QString> active_set() const;

    CoreContext* ctx_ = nullptr;
    AllModsFn all_mods_fn_;
    ActiveUuidsFn active_uuids_fn_;
    OrganizerDb& db_;
    std::optional<OrganizerState> state_;
};

}  // namespace px::modrim::organizer
