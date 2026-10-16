#include "core/organizer/resolve.hpp"

#include <algorithm>
#include <utility>

#include "core/providers/labels.hpp"

namespace px::modrim::organizer {

namespace {

struct Matcher {
    RuleOp op = RuleOp::kEquals;
    QString pattern;  // casefolded

    bool matches(const QString& value) const {
        switch (op) {
            case RuleOp::kPrefix: return value.startsWith(pattern);
            case RuleOp::kContains: return value.contains(pattern);
            case RuleOp::kEquals: return value == pattern;
        }
        return false;
    }
};

Matcher makeMatcher(RuleOp op, const QString& pattern) {
    return Matcher{op, pattern.toCaseFolded()};
}

struct FieldRule {
    RuleField field = RuleField::kName;
    Matcher matcher;
    qint64 folderId = kRootId;
};

// The casefolded value a rule field is matched against.
QString fieldValue(RuleField field, const QString& pid, const QString& name, const QString& author) {
    switch (field) {
        case RuleField::kPackageId: return pid.toCaseFolded();
        case RuleField::kName: return name.toCaseFolded();
        case RuleField::kAuthor: return author.toCaseFolded();
    }
    return {};
}

// Manual placements win over rules; the first matching rule by position wins.
class Resolver {
public:
    explicit Resolver(const OrganizerState& state) : m_placements(state.placements) {
        for (const Rule& rule : state.rules) {
            if (state.folders.contains(rule.folderId) && rule.folderId != kRootId) {
                m_rules.append(FieldRule{rule.field, makeMatcher(rule.op, rule.pattern), rule.folderId});
            }
        }
    }

    Resolution resolve(const std::optional<QString>& packageId, const QString& name,
                       const QString& author) const {
        if (!packageId) {
            return {kRootId, Placement::kNone};
        }
        const QString pid = packageId->toLower();
        if (const auto manual = m_placements.constFind(pid); manual != m_placements.cend()) {
            return {manual.value(), Placement::kManual};
        }
        for (const FieldRule& rule : m_rules) {
            if (rule.matcher.matches(fieldValue(rule.field, pid, name, author))) {
                return {rule.folderId, Placement::kRule};
            }
        }
        return {kRootId, Placement::kNone};
    }

private:
    const QHash<QString, qint64>& m_placements;
    QList<FieldRule> m_rules;
};

bool matchesQuery(const ModLeaf& leaf, const QString& providerId, const TreeQuery& query) {
    if (query.status == StatusFilter::kActive && !leaf.enabled) {
        return false;
    }
    if (query.status == StatusFilter::kInactive && leaf.enabled) {
        return false;
    }
    if (!query.providerIds.isEmpty() && !query.providerIds.contains(providerId)) {
        return false;
    }
    for (qint64 tagId : query.tagIds) {
        if (!leaf.tagIds.contains(tagId)) {
            return false;
        }
    }
    const QString text = query.text.trimmed().toCaseFolded();
    if (text.isEmpty()) {
        return true;
    }
    return leaf.name.trimmed().toCaseFolded().contains(text)
        || (leaf.packageId && leaf.packageId->contains(text))
        || leaf.author.toCaseFolded().contains(text);
}

// A folder with no mods counts as off, not on: "on" needs at least one enabled mod.
CheckState checkState(int total, int enabled) {
    if (total != 0 && enabled == total) {
        return CheckState::kOn;
    }
    if (enabled == 0) {
        return CheckState::kOff;
    }
    return CheckState::kPartial;
}

bool modLess(const ModLeaf& a, const ModLeaf& b) {
    return std::pair(a.name.toCaseFolded(), a.uuid) < std::pair(b.name.toCaseFolded(), b.uuid);
}

bool folderLess(const Folder& a, const Folder& b) {
    return std::pair(a.name.toCaseFolded(), a.id) < std::pair(b.name.toCaseFolded(), b.id);
}

// Builds the folder tree bottom-up. `visited` is shared across the whole walk, so a folder
// reachable twice is attached once.
struct TreeBuilder {
    const OrganizerState& state;
    const QHash<qint64, QList<Folder>>& childrenOf;
    const QHash<qint64, QList<ModLeaf>>& allByFolder;
    const QHash<qint64, QList<ModLeaf>>& visibleByFolder;
    const QSet<qint64>& ruled;
    bool filtering;
    QSet<qint64> visited;

    FolderNode build(const Folder& folder, int depth) {
        visited.insert(folder.id);
        FolderNode node;
        node.folder = folder;
        node.depth = depth;
        node.hasRules = ruled.contains(folder.id);

        QList<Folder> children = childrenOf.value(folder.id);
        std::sort(children.begin(), children.end(), folderLess);
        for (const Folder& child : children) {
            if (visited.contains(child.id)) {
                continue;
            }
            FolderNode sub = build(child, depth + 1);
            node.total += sub.total;
            node.enabled += sub.enabled;
            node.visibleTotal += sub.visibleTotal;
            if (!filtering || sub.visibleTotal != 0) {
                node.children.push_back(std::move(sub));
            }
        }

        const QList<ModLeaf> own = allByFolder.value(folder.id);
        QList<ModLeaf> visible = visibleByFolder.value(folder.id);
        std::sort(visible.begin(), visible.end(), modLess);
        int ownEnabled = 0;
        for (const ModLeaf& leaf : own) {
            ownEnabled += leaf.enabled ? 1 : 0;
        }
        node.ownTotal = static_cast<int>(own.size());
        node.ownEnabled = ownEnabled;
        node.total += node.ownTotal;
        node.enabled += ownEnabled;
        node.visibleTotal += static_cast<int>(visible.size());
        node.check = checkState(node.total, node.enabled);
        node.mods = std::move(visible);
        return node;
    }
};

}  // namespace

bool TreeQuery::isEmpty() const {
    return text.trimmed().isEmpty() && tagIds.isEmpty() && status == StatusFilter::kAll && providerIds.isEmpty();
}

TreeQuery TreeFilter::query(const QString& text) const {
    TreeQuery query;
    query.text = text;
    query.status = status;
    if (providerId) {
        query.providerIds.insert(*providerId);
    }
    if (tagId) {
        query.tagIds.insert(*tagId);
    }
    return query;
}

CheckState FolderNode::ownCheck() const {
    return checkState(ownTotal, ownEnabled);
}

QList<const FolderNode*> FolderNode::walk() const {
    QList<const FolderNode*> nodes{this};
    for (const FolderNode& child : children) {
        nodes += child.walk();
    }
    return nodes;
}

QList<ModLeaf> FolderNode::allMods() const {
    QList<ModLeaf> modsOut;
    for (const FolderNode* node : walk()) {
        modsOut += node->mods;
    }
    return modsOut;
}

std::optional<QString> modPackageId(const ListedMod& mod) {
    if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod)) {
        return about->packageId.str();
    }
    return std::nullopt;
}

QString modAuthor(const ListedMod& mod) {
    if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod)) {
        return about->authors.join(QStringLiteral(", "));
    }
    return {};
}

Resolution folderFor(const OrganizerState& state, const std::optional<QString>& packageId,
                      const QString& name, const QString& author) {
    return Resolver(state).resolve(packageId, name, author);
}

RulePreview previewRuleMatches(const OrganizerState& state, const ModsByUuid& mods,
                                 const QList<RuleSpec>& specs) {
    const Resolver current(state);
    // A spec with a blank pattern, an unknown folder or the root matches nothing. The
    // matcher still uses the raw pattern, as the legacy preview did.
    std::vector<std::optional<FieldRule>> candidates;
    candidates.reserve(static_cast<std::size_t>(specs.size()));
    for (const RuleSpec& spec : specs) {
        const bool usable = !spec.pattern.trimmed().isEmpty() && state.folders.contains(spec.folderId) &&
                            spec.folderId != kRootId;
        candidates.push_back(usable ? std::optional(FieldRule{spec.field, makeMatcher(spec.op, spec.pattern),
                                                              spec.folderId})
                                    : std::nullopt);
    }

    RulePreview preview;
    preview.counts.assign(specs.size(), 0);
    for (const ListedModPtr& mod : mods) {
        const auto pid = modPackageId(*mod);
        if (!pid) {
            continue;
        }
        const QString author = modAuthor(*mod);
        for (qsizetype index = 0; index < specs.size(); ++index) {
            const std::optional<FieldRule>& candidate = candidates[static_cast<std::size_t>(index)];
            if (!candidate || !candidate->matcher.matches(fieldValue(candidate->field, *pid, mod->name, author))) {
                continue;
            }
            preview.counts[index] += 1;
            if (!state.placements.contains(*pid) && current.resolve(pid, mod->name, author).folderId == kRootId) {
                preview.assignable += 1;
            }
            break;
        }
    }
    return preview;
}

QList<TreeFilter> treeFilters(const ModsByUuid& mods, const QSet<QString>& active,
                               const OrganizerState* state) {
    const int total = static_cast<int>(mods.size());
    int enabled = 0;
    QHash<QString, int> perProvider;
    for (auto it = mods.cbegin(); it != mods.cend(); ++it) {
        enabled += active.contains(it.key()) ? 1 : 0;
        perProvider[it.value()->providerId] += 1;
    }

    const auto make = [](QString key, QString label, int count) {
        TreeFilter filter;
        filter.key = std::move(key);
        filter.label = std::move(label);
        filter.count = count;
        return filter;
    };

    TreeFilter statusActive = make(QStringLiteral("active"), QStringLiteral("Active"), enabled);
    statusActive.status = StatusFilter::kActive;
    TreeFilter statusInactive = make(QStringLiteral("inactive"), QStringLiteral("Inactive"), total - enabled);
    statusInactive.status = StatusFilter::kInactive;
    QList<TreeFilter> filters{make(QStringLiteral("all"), QStringLiteral("All"), total), std::move(statusActive),
                              std::move(statusInactive)};

    std::vector<std::pair<QString, QString>> labelled;  // (label, provider id)
    for (auto it = perProvider.cbegin(); it != perProvider.cend(); ++it) {
        labelled.emplace_back(providerLabel(it.key()), it.key());
    }
    std::sort(labelled.begin(), labelled.end());
    for (const auto& [label, pid] : labelled) {
        TreeFilter provider = make(QStringLiteral("provider:%1").arg(pid), label, perProvider.value(pid));
        provider.providerId = pid;
        filters.append(std::move(provider));
    }

    if (state && !state->tags.isEmpty()) {
        QHash<qint64, int> tagCounts;
        for (const auto& [id, tag] : state->tags.asKeyValueRange()) {
            tagCounts.insert(id, 0);
        }
        for (const ListedModPtr& mod : mods) {
            const auto pid = modPackageId(*mod);
            if (!pid) {
                continue;
            }
            for (qint64 tagId : state->modTags.value(*pid)) {
                if (tagCounts.contains(tagId)) {
                    tagCounts[tagId] += 1;
                }
            }
        }

        std::vector<Tag> sortedTags;
        for (const Tag& tag : state->tags) {
            sortedTags.push_back(tag);
        }
        std::sort(sortedTags.begin(), sortedTags.end(), [](const Tag& a, const Tag& b) {
            return std::pair(a.name.toCaseFolded(), a.id) < std::pair(b.name.toCaseFolded(), b.id);
        });
        for (const Tag& tag : sortedTags) {
            TreeFilter tagFilter = make(QStringLiteral("tag:%1").arg(tag.id), tag.name, tagCounts.value(tag.id));
            tagFilter.tagId = tag.id;
            filters.append(std::move(tagFilter));
        }
    }
    return filters;
}

FolderNode buildTree(const OrganizerState& state, const ModsByUuid& mods, const QSet<QString>& active,
                      const std::optional<TreeQuery>& query) {
    const bool filtering = query && !query->isEmpty();
    const Resolver resolver(state);

    QHash<qint64, QList<ModLeaf>> allByFolder;
    QHash<qint64, QList<ModLeaf>> visibleByFolder;
    for (auto it = mods.cbegin(); it != mods.cend(); ++it) {
        const ListedMod& mod = *it.value();
        const QString& uuid = it.key();
        const auto pid = modPackageId(mod);
        const QString author = modAuthor(mod);
        Resolution where = resolver.resolve(pid, mod.name, author);
        if (!state.folders.contains(where.folderId)) {
            where = Resolution{kRootId, Placement::kNone};
        }
        const ModLeaf leaf{
            .uuid = uuid,
            .packageId = pid,
            .name = mod.name,
            .author = author,
            .enabled = active.contains(uuid),
            .tagIds = pid ? state.modTags.value(*pid) : QSet<qint64>{},
            .placement = where.placement,
        };
        allByFolder[where.folderId].append(leaf);
        if (!filtering || matchesQuery(leaf, mod.providerId, *query)) {
            visibleByFolder[where.folderId].append(leaf);
        }
    }

    QHash<qint64, QList<Folder>> childrenOf;
    for (const Folder& folder : state.folders) {
        if (folder.parentId) {
            childrenOf[*folder.parentId].append(folder);
        }
    }
    QSet<qint64> ruled;
    for (const Rule& rule : state.rules) {
        ruled.insert(rule.folderId);
    }

    TreeBuilder builder{state, childrenOf, allByFolder, visibleByFolder, ruled, filtering, {}};
    return builder.build(state.folders.value(kRootId), 0);
}

}  // namespace px::modrim::organizer
