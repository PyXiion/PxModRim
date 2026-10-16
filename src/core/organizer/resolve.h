#pragma once

#include <optional>
#include <vector>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "core/models/metadata/structures.h"
#include "core/organizer/models.h"

namespace px::modrim::organizer {

// Discovered mods keyed by uuid.
using ModsByUuid = QHash<QString, ListedModPtr>;

// Where a mod's folder came from: a manual placement, an auto-folder rule, or nowhere (root).
enum class Placement { Manual, Rule, None };
enum class CheckState { On, Off, Partial };
enum class StatusFilter { All, Active, Inactive };

struct Resolution {
    qint64 folder_id = ROOT_ID;
    Placement placement = Placement::None;
};

struct TreeQuery {
    QString text;
    QSet<qint64> tag_ids;
    StatusFilter status = StatusFilter::All;
    QSet<QString> provider_ids;

    bool is_empty() const;
};

// A sidebar preset (status, provider, or tag) and the mods it selects.
struct TreeFilter {
    QString key;
    QString label;
    int count = 0;
    StatusFilter status = StatusFilter::All;
    std::optional<QString> provider_id;
    std::optional<qint64> tag_id;

    TreeQuery query(const QString& text = {}) const;
};

struct ModLeaf {
    QString uuid;
    std::optional<QString> package_id;
    QString name;
    QString author;
    bool enabled = false;
    QSet<qint64> tag_ids;
    Placement placement = Placement::None;
};

struct FolderNode {
    Folder folder;
    int depth = 0;
    // std::vector: QList cannot hold its own incomplete element type.
    std::vector<FolderNode> children;
    QList<ModLeaf> mods;
    int total = 0;
    int enabled = 0;
    CheckState check = CheckState::Off;
    int visible_total = 0;
    int own_total = 0;
    int own_enabled = 0;
    bool has_rules = false;

    // Check state of the folder's direct mods only (the root's Ungrouped).
    CheckState own_check() const;
    // This node followed by all descendants, in pre-order.
    QList<const FolderNode*> walk() const;
    QList<ModLeaf> all_mods() const;
};

// First-match counts for a draft rule list, and the ungrouped mods it would assign.
struct RulePreview {
    QList<int> counts;
    int assignable = 0;
};

// Lowercased package id for About.xml mods; nullopt for mods without one.
std::optional<QString> mod_package_id(const ListedMod& mod);
QString mod_author(const ListedMod& mod);

Resolution folder_for(const OrganizerState& state, const std::optional<QString>& package_id,
                      const QString& name, const QString& author);

RulePreview preview_rule_matches(const OrganizerState& state, const ModsByUuid& mods,
                                 const QList<RuleSpec>& specs);

// `state` may be null, in which case no tag filters are produced.
QList<TreeFilter> tree_filters(const ModsByUuid& mods, const QSet<QString>& active,
                               const OrganizerState* state);

FolderNode build_tree(const OrganizerState& state, const ModsByUuid& mods, const QSet<QString>& active,
                      const std::optional<TreeQuery>& query = std::nullopt);

}  // namespace px::modrim::organizer
