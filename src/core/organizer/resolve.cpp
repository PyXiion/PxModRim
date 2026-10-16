#include "core/organizer/resolve.h"

#include <algorithm>
#include <utility>

#include "core/providers/labels.h"

namespace px::modrim::organizer {

namespace {

struct Matcher {
    RuleOp op = RuleOp::Equals;
    QString pattern;  // casefolded

    bool matches(const QString& value) const {
        switch (op) {
            case RuleOp::Prefix: return value.startsWith(pattern);
            case RuleOp::Contains: return value.contains(pattern);
            case RuleOp::Equals: return value == pattern;
        }
        return false;
    }
};

Matcher make_matcher(RuleOp op, const QString& pattern) {
    return Matcher{op, pattern.toCaseFolded()};
}

struct FieldRule {
    RuleField field = RuleField::Name;
    Matcher matcher;
    qint64 folder_id = ROOT_ID;
};

// The casefolded value a rule field is matched against.
QString field_value(RuleField field, const QString& pid, const QString& name, const QString& author) {
    switch (field) {
        case RuleField::PackageId: return pid.toCaseFolded();
        case RuleField::Name: return name.toCaseFolded();
        case RuleField::Author: return author.toCaseFolded();
    }
    return {};
}

// Manual placements win over rules; the first matching rule by position wins.
class Resolver {
public:
    explicit Resolver(const OrganizerState& state) : placements_(state.placements) {
        for (const Rule& rule : state.rules) {
            if (state.folders.contains(rule.folder_id) && rule.folder_id != ROOT_ID) {
                rules_.append(FieldRule{rule.field, make_matcher(rule.op, rule.pattern), rule.folder_id});
            }
        }
    }

    Resolution resolve(const std::optional<QString>& package_id, const QString& name,
                       const QString& author) const {
        if (!package_id) {
            return {ROOT_ID, Placement::None};
        }
        const QString pid = package_id->toLower();
        if (const auto manual = placements_.constFind(pid); manual != placements_.cend()) {
            return {manual.value(), Placement::Manual};
        }
        for (const FieldRule& rule : rules_) {
            if (rule.matcher.matches(field_value(rule.field, pid, name, author))) {
                return {rule.folder_id, Placement::Rule};
            }
        }
        return {ROOT_ID, Placement::None};
    }

private:
    const QHash<QString, qint64>& placements_;
    QList<FieldRule> rules_;
};

bool matches_query(const ModLeaf& leaf, const QString& provider_id, const TreeQuery& query) {
    if (query.status == StatusFilter::Active && !leaf.enabled) {
        return false;
    }
    if (query.status == StatusFilter::Inactive && leaf.enabled) {
        return false;
    }
    if (!query.provider_ids.isEmpty() && !query.provider_ids.contains(provider_id)) {
        return false;
    }
    for (qint64 tag_id : query.tag_ids) {
        if (!leaf.tag_ids.contains(tag_id)) {
            return false;
        }
    }
    const QString text = query.text.trimmed().toCaseFolded();
    if (text.isEmpty()) {
        return true;
    }
    return leaf.name.trimmed().toCaseFolded().contains(text)
        || (leaf.package_id && leaf.package_id->contains(text))
        || leaf.author.toCaseFolded().contains(text);
}

// A folder with no mods counts as off, not on: "on" needs at least one enabled mod.
CheckState check_state(int total, int enabled) {
    if (total != 0 && enabled == total) {
        return CheckState::On;
    }
    if (enabled == 0) {
        return CheckState::Off;
    }
    return CheckState::Partial;
}

bool mod_less(const ModLeaf& a, const ModLeaf& b) {
    return std::pair(a.name.toCaseFolded(), a.uuid) < std::pair(b.name.toCaseFolded(), b.uuid);
}

bool folder_less(const Folder& a, const Folder& b) {
    return std::pair(a.name.toCaseFolded(), a.id) < std::pair(b.name.toCaseFolded(), b.id);
}

// Builds the folder tree bottom-up. `visited` is shared across the whole walk, so a folder
// reachable twice is attached once.
struct TreeBuilder {
    const OrganizerState& state;
    const QHash<qint64, QList<Folder>>& children_of;
    const QHash<qint64, QList<ModLeaf>>& all_by_folder;
    const QHash<qint64, QList<ModLeaf>>& visible_by_folder;
    const QSet<qint64>& ruled;
    bool filtering;
    QSet<qint64> visited;

    FolderNode build(const Folder& folder, int depth) {
        visited.insert(folder.id);
        FolderNode node;
        node.folder = folder;
        node.depth = depth;
        node.has_rules = ruled.contains(folder.id);

        QList<Folder> children = children_of.value(folder.id);
        std::sort(children.begin(), children.end(), folder_less);
        for (const Folder& child : children) {
            if (visited.contains(child.id)) {
                continue;
            }
            FolderNode sub = build(child, depth + 1);
            node.total += sub.total;
            node.enabled += sub.enabled;
            node.visible_total += sub.visible_total;
            if (!filtering || sub.visible_total != 0) {
                node.children.push_back(std::move(sub));
            }
        }

        const QList<ModLeaf> own = all_by_folder.value(folder.id);
        QList<ModLeaf> visible = visible_by_folder.value(folder.id);
        std::sort(visible.begin(), visible.end(), mod_less);
        int own_enabled = 0;
        for (const ModLeaf& leaf : own) {
            own_enabled += leaf.enabled ? 1 : 0;
        }
        node.own_total = static_cast<int>(own.size());
        node.own_enabled = own_enabled;
        node.total += node.own_total;
        node.enabled += own_enabled;
        node.visible_total += static_cast<int>(visible.size());
        node.check = check_state(node.total, node.enabled);
        node.mods = std::move(visible);
        return node;
    }
};

}  // namespace

bool TreeQuery::is_empty() const {
    return text.trimmed().isEmpty() && tag_ids.isEmpty() && status == StatusFilter::All && provider_ids.isEmpty();
}

TreeQuery TreeFilter::query(const QString& text) const {
    TreeQuery query;
    query.text = text;
    query.status = status;
    if (provider_id) {
        query.provider_ids.insert(*provider_id);
    }
    if (tag_id) {
        query.tag_ids.insert(*tag_id);
    }
    return query;
}

CheckState FolderNode::own_check() const {
    return check_state(own_total, own_enabled);
}

QList<const FolderNode*> FolderNode::walk() const {
    QList<const FolderNode*> nodes{this};
    for (const FolderNode& child : children) {
        nodes += child.walk();
    }
    return nodes;
}

QList<ModLeaf> FolderNode::all_mods() const {
    QList<ModLeaf> mods_out;
    for (const FolderNode* node : walk()) {
        mods_out += node->mods;
    }
    return mods_out;
}

std::optional<QString> mod_package_id(const ListedMod& mod) {
    if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod)) {
        return about->package_id.str();
    }
    return std::nullopt;
}

QString mod_author(const ListedMod& mod) {
    if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod)) {
        return about->authors.join(QStringLiteral(", "));
    }
    return {};
}

Resolution folder_for(const OrganizerState& state, const std::optional<QString>& package_id,
                      const QString& name, const QString& author) {
    return Resolver(state).resolve(package_id, name, author);
}

RulePreview preview_rule_matches(const OrganizerState& state, const ModsByUuid& mods,
                                 const QList<RuleSpec>& specs) {
    const Resolver current(state);
    // A spec with a blank pattern, an unknown folder or the root matches nothing. The
    // matcher still uses the raw pattern, as the legacy preview did.
    std::vector<std::optional<FieldRule>> candidates;
    candidates.reserve(static_cast<std::size_t>(specs.size()));
    for (const RuleSpec& spec : specs) {
        const bool usable = !spec.pattern.trimmed().isEmpty() && state.folders.contains(spec.folder_id) &&
                            spec.folder_id != ROOT_ID;
        candidates.push_back(usable ? std::optional(FieldRule{spec.field, make_matcher(spec.op, spec.pattern),
                                                              spec.folder_id})
                                    : std::nullopt);
    }

    RulePreview preview;
    preview.counts.assign(specs.size(), 0);
    for (const ListedModPtr& mod : mods) {
        const auto pid = mod_package_id(*mod);
        if (!pid) {
            continue;
        }
        const QString author = mod_author(*mod);
        for (qsizetype index = 0; index < specs.size(); ++index) {
            const std::optional<FieldRule>& candidate = candidates[static_cast<std::size_t>(index)];
            if (!candidate || !candidate->matcher.matches(field_value(candidate->field, *pid, mod->name, author))) {
                continue;
            }
            preview.counts[index] += 1;
            if (!state.placements.contains(*pid) && current.resolve(pid, mod->name, author).folder_id == ROOT_ID) {
                preview.assignable += 1;
            }
            break;
        }
    }
    return preview;
}

QList<TreeFilter> tree_filters(const ModsByUuid& mods, const QSet<QString>& active,
                               const OrganizerState* state) {
    const int total = static_cast<int>(mods.size());
    int enabled = 0;
    QHash<QString, int> per_provider;
    for (auto it = mods.cbegin(); it != mods.cend(); ++it) {
        enabled += active.contains(it.key()) ? 1 : 0;
        per_provider[it.value()->provider_id] += 1;
    }

    const auto make = [](QString key, QString label, int count) {
        TreeFilter filter;
        filter.key = std::move(key);
        filter.label = std::move(label);
        filter.count = count;
        return filter;
    };

    TreeFilter status_active = make(QStringLiteral("active"), QStringLiteral("Active"), enabled);
    status_active.status = StatusFilter::Active;
    TreeFilter status_inactive = make(QStringLiteral("inactive"), QStringLiteral("Inactive"), total - enabled);
    status_inactive.status = StatusFilter::Inactive;
    QList<TreeFilter> filters{make(QStringLiteral("all"), QStringLiteral("All"), total), std::move(status_active),
                              std::move(status_inactive)};

    std::vector<std::pair<QString, QString>> labelled;  // (label, provider id)
    for (auto it = per_provider.cbegin(); it != per_provider.cend(); ++it) {
        labelled.emplace_back(providerLabel(it.key()), it.key());
    }
    std::sort(labelled.begin(), labelled.end());
    for (const auto& [label, pid] : labelled) {
        TreeFilter provider = make(QStringLiteral("provider:%1").arg(pid), label, per_provider.value(pid));
        provider.provider_id = pid;
        filters.append(std::move(provider));
    }

    if (state && !state->tags.isEmpty()) {
        QHash<qint64, int> tag_counts;
        for (const auto& [id, tag] : state->tags.asKeyValueRange()) {
            tag_counts.insert(id, 0);
        }
        for (const ListedModPtr& mod : mods) {
            const auto pid = mod_package_id(*mod);
            if (!pid) {
                continue;
            }
            for (qint64 tag_id : state->mod_tags.value(*pid)) {
                if (tag_counts.contains(tag_id)) {
                    tag_counts[tag_id] += 1;
                }
            }
        }

        std::vector<Tag> sorted_tags;
        for (const Tag& tag : state->tags) {
            sorted_tags.push_back(tag);
        }
        std::sort(sorted_tags.begin(), sorted_tags.end(), [](const Tag& a, const Tag& b) {
            return std::pair(a.name.toCaseFolded(), a.id) < std::pair(b.name.toCaseFolded(), b.id);
        });
        for (const Tag& tag : sorted_tags) {
            TreeFilter tag_filter = make(QStringLiteral("tag:%1").arg(tag.id), tag.name, tag_counts.value(tag.id));
            tag_filter.tag_id = tag.id;
            filters.append(std::move(tag_filter));
        }
    }
    return filters;
}

FolderNode build_tree(const OrganizerState& state, const ModsByUuid& mods, const QSet<QString>& active,
                      const std::optional<TreeQuery>& query) {
    const bool filtering = query && !query->is_empty();
    const Resolver resolver(state);

    QHash<qint64, QList<ModLeaf>> all_by_folder;
    QHash<qint64, QList<ModLeaf>> visible_by_folder;
    for (auto it = mods.cbegin(); it != mods.cend(); ++it) {
        const ListedMod& mod = *it.value();
        const QString& uuid = it.key();
        const auto pid = mod_package_id(mod);
        const QString author = mod_author(mod);
        Resolution where = resolver.resolve(pid, mod.name, author);
        if (!state.folders.contains(where.folder_id)) {
            where = Resolution{ROOT_ID, Placement::None};
        }
        const ModLeaf leaf{
            .uuid = uuid,
            .package_id = pid,
            .name = mod.name,
            .author = author,
            .enabled = active.contains(uuid),
            .tag_ids = pid ? state.mod_tags.value(*pid) : QSet<qint64>{},
            .placement = where.placement,
        };
        all_by_folder[where.folder_id].append(leaf);
        if (!filtering || matches_query(leaf, mod.provider_id, *query)) {
            visible_by_folder[where.folder_id].append(leaf);
        }
    }

    QHash<qint64, QList<Folder>> children_of;
    for (const Folder& folder : state.folders) {
        if (folder.parent_id) {
            children_of[*folder.parent_id].append(folder);
        }
    }
    QSet<qint64> ruled;
    for (const Rule& rule : state.rules) {
        ruled.insert(rule.folder_id);
    }

    TreeBuilder builder{state, children_of, all_by_folder, visible_by_folder, ruled, filtering, {}};
    return builder.build(state.folders.value(ROOT_ID), 0);
}

}  // namespace px::modrim::organizer
