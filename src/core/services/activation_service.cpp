#include "core/services/activation_service.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

#include <QHash>
#include <QSet>

#include "core/checker/graph.h"
#include "core/context.h"
#include "core/models/metadata/structures.h"
#include "core/services/diagnostics_service.h"
#include "core/sort/config.h"
#include "core/sort/models.h"

namespace px::core {
namespace {

using checker::ConstraintEdge;
using checker::ConstraintGraph;
using checker::EdgeType;
using checker::PackageId;
using ModMap = QHash<PackageId, checker::AboutXmlModPtr>;
using Edge = std::pair<PackageId, PackageId>;

// The About.xml mod behind `uuid`, or null when it is unknown or has no About.xml.
checker::AboutXmlModPtr about_mod(const QHash<QString, ListedModPtr>& all_mods, const QString& uuid) {
    const auto it = all_mods.constFind(uuid);
    return it == all_mods.cend() ? nullptr : std::dynamic_pointer_cast<AboutXmlMod>(it.value());
}

QStringList enable_candidates(const QHash<QString, ListedModPtr>& all_mods,
                              const QStringList& enable,
                              const QSet<QString>& disable_set,
                              const QSet<QString>& active_set) {
    QStringList candidates;
    QSet<QString> seen;
    for (const QString& uuid : enable) {
        if (seen.contains(uuid) || disable_set.contains(uuid)) {
            continue;
        }
        seen.insert(uuid);
        if (all_mods.contains(uuid) && !active_set.contains(uuid)) {
            candidates.append(uuid);
        }
    }
    return candidates;
}

// (before, after) pairs between mods touching any of `candidates`. Edges through ids outside
// `mods` (uninstalled optional mods) are dropped so they cannot chain unrelated mods together.
QList<Edge> build_edges(const ModMap& mods,
                        const QList<PackageId>& order,
                        const QList<PackageId>& candidates,
                        const sort::SortSettings& settings,
                        const sort::CommunityRules& community_rules) {
    ConstraintGraph graph;
    graph.build(mods, order, settings, &community_rules);
    // Placement ignores active-to-active edges, so only edges incident to a candidate matter;
    // the graph still resolves the full alternative map.
    QSet<ConstraintEdge> incident;
    for (const PackageId& pid : candidates) {
        incident.unite(graph.outgoing(pid));
        incident.unite(graph.incoming(pid));
    }

    QList<Edge> edges;
    for (const ConstraintEdge& edge : incident) {
        PackageId before;
        PackageId after;
        switch (edge.type) {
        case EdgeType::LoadBefore:
            before = edge.source;
            after = edge.target;
            break;
        case EdgeType::Dependency:
        case EdgeType::LoadAfter:
            before = edge.target;
            after = edge.source;
            break;
        default:
            continue;
        }
        if (before != after && mods.contains(before) && mods.contains(after)) {
            edges.append({before, after});
        }
    }
    return edges;
}

// Iterative Tarjan SCC. Each component lists its members in `nodes` order.
QList<QStringList> strongly_connected(const QStringList& nodes,
                                      const QHash<QString, QSet<QString>>& adjacent) {
    struct Frame {
        QString node;
        QStringList children;
        qsizetype next = 0;
    };

    QHash<QString, int> index_of;
    QHash<QString, int> lowlink;
    QStringList stack;
    QSet<QString> on_stack;
    QList<QStringList> components;
    QList<Frame> work;

    const auto visit = [&](const QString& node) {
        const int index = static_cast<int>(index_of.size());
        index_of.insert(node, index);
        lowlink.insert(node, index);
        stack.append(node);
        on_stack.insert(node);
        const QSet<QString> successors = adjacent.value(node);
        work.append(Frame{node, QStringList(successors.cbegin(), successors.cend()), 0});
    };

    for (const QString& root : nodes) {
        if (index_of.contains(root)) {
            continue;
        }
        visit(root);
        while (!work.isEmpty()) {
            Frame& frame = work.last();
            if (frame.next < frame.children.size()) {
                const QString child = frame.children[frame.next++];
                if (!index_of.contains(child)) {
                    // Invalidates `frame`; the loop re-reads work.last().
                    visit(child);
                } else if (on_stack.contains(child)) {
                    lowlink[frame.node] = std::min(lowlink.value(frame.node), index_of.value(child));
                }
                continue;
            }

            // Every child is done: close this node.
            const QString node = frame.node;
            work.removeLast();
            if (!work.isEmpty()) {
                const QString parent = work.last().node;
                lowlink[parent] = std::min(lowlink.value(parent), lowlink.value(node));
            }
            if (lowlink.value(node) == index_of.value(node)) {
                QStringList component;
                QString member;
                do {
                    member = stack.takeLast();
                    on_stack.remove(member);
                    component.append(member);
                } while (member != node);
                components.append(component);
            }
        }
    }
    return components;
}

// Slots enable candidates into gaps of a fixed active order.
//
// Gap i means "just before new_active[i]"; gap new_active.size() is the end. Bounds come only from
// direct edges to active mods, propagated through batch candidates, because the active order
// itself may already violate constraints and must not be re-derived transitively.
class Placement {
public:
    Placement(QStringList new_active,
              QStringList candidates,
              const QHash<QString, ListedModPtr>& all_mods,
              const sort::SortSettings& settings,
              const sort::CommunityRules& community_rules);

    QStringList assemble() const;

private:
    void add_edge(const PackageId& before, const PackageId& after);
    std::pair<QStringList, QSet<QString>> order_batch() const;
    QHash<QString, int> propagated_ceilings(const QStringList& ordered,
                                            const QSet<QString>& cyclic) const;
    int gap_for(const QString& uuid,
                const QSet<QString>& cyclic,
                const QHash<QString, int>& ceilings,
                const QHash<QString, int>& gaps) const;

    QStringList new_active_;
    QStringList candidates_;
    int end_;

    QHash<PackageId, int> first_pos_;
    QHash<PackageId, int> last_pos_;
    QHash<PackageId, QStringList> uuids_by_pid_;

    // Batch-internal adjacency plus direct bounds against active mods.
    QHash<QString, QSet<QString>> after_;
    QHash<QString, QSet<QString>> before_;
    QHash<QString, int> floor_;
    QHash<QString, int> ceiling_;
};

Placement::Placement(QStringList new_active,
                     QStringList candidates,
                     const QHash<QString, ListedModPtr>& all_mods,
                     const sort::SortSettings& settings,
                     const sort::CommunityRules& community_rules)
    : new_active_(std::move(new_active)),
      candidates_(std::move(candidates)),
      end_(static_cast<int>(new_active_.size())) {
    ModMap mods;
    QList<PackageId> order;
    for (qsizetype index = 0; index < new_active_.size(); ++index) {
        const checker::AboutXmlModPtr mod = about_mod(all_mods, new_active_[index]);
        if (!mod) {
            continue;
        }
        const PackageId& pid = mod->package_id;
        if (!first_pos_.contains(pid)) {
            first_pos_.insert(pid, static_cast<int>(index));
        }
        last_pos_.insert(pid, static_cast<int>(index));
        if (!mods.contains(pid)) {
            mods.insert(pid, mod);
            order.append(pid);
        }
    }

    for (const QString& uuid : candidates_) {
        after_.insert(uuid, {});
        before_.insert(uuid, {});
        const checker::AboutXmlModPtr mod = about_mod(all_mods, uuid);
        if (!mod) {
            continue;
        }
        const PackageId& pid = mod->package_id;
        uuids_by_pid_[pid].append(uuid);
        if (!mods.contains(pid)) {
            mods.insert(pid, mod);
            order.append(pid);
        }
    }

    if (!uuids_by_pid_.isEmpty()) {
        for (const Edge& edge :
             build_edges(mods, order, uuids_by_pid_.keys(), settings, community_rules)) {
            add_edge(edge.first, edge.second);
        }
    }
}

void Placement::add_edge(const PackageId& before, const PackageId& after) {
    const QStringList before_uuids = uuids_by_pid_.value(before);
    const QStringList after_uuids = uuids_by_pid_.value(after);
    for (const QString& b : before_uuids) {
        for (const QString& a : after_uuids) {
            if (a != b) {
                after_[b].insert(a);
                before_[a].insert(b);
            }
        }
        if (const auto first = first_pos_.constFind(after); first != first_pos_.cend()) {
            ceiling_[b] = std::min(ceiling_.value(b, first.value()), first.value());
        }
    }
    if (const auto last = last_pos_.constFind(before); last != last_pos_.cend()) {
        for (const QString& a : after_uuids) {
            floor_[a] = std::max(floor_.value(a, last.value() + 1), last.value() + 1);
        }
    }
}

QStringList Placement::assemble() const {
    const auto [ordered, cyclic] = order_batch();
    const QHash<QString, int> ceilings = propagated_ceilings(ordered, cyclic);
    QList<QStringList> buckets(end_ + 1);
    QHash<QString, int> gaps;
    for (const QString& uuid : ordered) {
        const int gap = gap_for(uuid, cyclic, ceilings, gaps);
        gaps.insert(uuid, gap);
        buckets[gap].append(uuid);
    }

    QStringList result;
    for (qsizetype index = 0; index < new_active_.size(); ++index) {
        result.append(buckets[index]);
        result.append(new_active_[index]);
    }
    result.append(buckets[end_]);
    return result;
}

int Placement::gap_for(const QString& uuid,
                       const QSet<QString>& cyclic,
                       const QHash<QString, int>& ceilings,
                       const QHash<QString, int>& gaps) const {
    if (cyclic.contains(uuid)) {
        return end_;
    }
    // Befores of an acyclic candidate sit in earlier components, so their gaps are final.
    std::optional<int> earliest;
    const auto raise_floor = [&earliest](int floor) {
        earliest = earliest ? std::max(*earliest, floor) : floor;
    };
    for (const QString& b : before_.value(uuid)) {
        raise_floor(gaps.value(b));
    }
    if (const auto floor = floor_.constFind(uuid); floor != floor_.cend()) {
        raise_floor(floor.value());
    }

    std::optional<int> ceiling;
    if (const auto found = ceilings.constFind(uuid); found != ceilings.cend()) {
        ceiling = found.value();
    }
    if (earliest) {
        if (ceiling && *earliest > *ceiling) {
            return end_;
        }
        return *earliest;
    }
    return ceiling ? *ceiling : end_;
}

QHash<QString, int> Placement::propagated_ceilings(const QStringList& ordered,
                                                   const QSet<QString>& cyclic) const {
    // Earliest active position each candidate must precede, via batch chains.
    QHash<QString, int> ceilings;
    for (auto it = ordered.crbegin(); it != ordered.crend(); ++it) {
        if (cyclic.contains(*it)) {
            continue;
        }
        std::optional<int> bound;
        const auto tighten = [&bound](int value) {
            bound = bound ? std::min(*bound, value) : value;
        };
        for (const QString& a : after_.value(*it)) {
            if (const auto found = ceilings.constFind(a); found != ceilings.cend()) {
                tighten(found.value());
            }
        }
        if (const auto own = ceiling_.constFind(*it); own != ceiling_.cend()) {
            tighten(own.value());
        }
        if (bound) {
            ceilings.insert(*it, *bound);
        }
    }
    return ceilings;
}

// Topologically orders candidates by input order; cycle members are placed as a unit.
std::pair<QStringList, QSet<QString>> Placement::order_batch() const {
    auto components = strongly_connected(candidates_, after_);

    QHash<QString, int> input_index;
    for (qsizetype index = 0; index < candidates_.size(); ++index) {
        input_index.insert(candidates_[index], static_cast<int>(index));
    }

    QHash<QString, int> component_of;
    QSet<QString> cyclic;
    for (int index = 0; index < components.size(); ++index) {
        QStringList& members = components[index];
        std::sort(members.begin(), members.end(), [&input_index](const QString& a, const QString& b) {
            return input_index.value(a) < input_index.value(b);
        });
        for (const QString& member : members) {
            component_of.insert(member, index);
        }
        if (members.size() > 1) {
            cyclic.unite(QSet<QString>(members.cbegin(), members.cend()));
        }
    }

    QList<QSet<int>> successors(components.size());
    QList<int> indegree(components.size(), 0);
    for (auto it = after_.cbegin(); it != after_.cend(); ++it) {
        const int source = component_of.value(it.key());
        for (const QString& successor : it.value()) {
            const int target = component_of.value(successor);
            if (target != source && !successors[source].contains(target)) {
                successors[source].insert(target);
                ++indegree[target];
            }
        }
    }

    // Ready components pop in input order of their first member.
    using Ready = std::pair<int, int>;
    std::priority_queue<Ready, std::vector<Ready>, std::greater<>> ready;
    for (int index = 0; index < components.size(); ++index) {
        if (indegree[index] == 0) {
            ready.emplace(input_index.value(components[index].front()), index);
        }
    }
    QStringList ordered;
    while (!ready.empty()) {
        const int index = ready.top().second;
        ready.pop();
        ordered.append(components[index]);
        for (const int target : successors[index]) {
            if (--indegree[target] == 0) {
                ready.emplace(input_index.value(components[target].front()), target);
            }
        }
    }
    return {std::move(ordered), std::move(cyclic)};
}

}  // namespace

ActivationService::ActivationService(CoreContext& ctx) : ctx_(ctx) {}

QStringList ActivationService::dependents_of(const QStringList& uuids,
                                             const QStringList& enable) const {
    const QSet<QString> excluded(uuids.cbegin(), uuids.cend());
    const QHash<QString, ListedModPtr>& all_mods = ctx_.allMods();
    const QStringList& active = ctx_.activeUuids();

    QSet<PackageId> remaining;
    const auto keep = [&](const QString& uuid) {
        if (excluded.contains(uuid)) {
            return;
        }
        if (const checker::AboutXmlModPtr mod = about_mod(all_mods, uuid)) {
            remaining.insert(mod->package_id);
        }
    };
    for (const QString& uuid : active) {
        keep(uuid);
    }
    for (const QString& uuid : enable) {
        keep(uuid);
    }

    QSet<PackageId> roots;
    for (const QString& uuid : excluded) {
        const checker::AboutXmlModPtr mod = about_mod(all_mods, uuid);
        if (mod && !remaining.contains(mod->package_id)) {
            roots.insert(mod->package_id);
        }
    }
    if (roots.isEmpty()) {
        return {};
    }

    const ConstraintGraph& graph = ctx_.diagnosticsService().constraint_graph();
    QSet<PackageId> dependents;
    QList<PackageId> pending(roots.cbegin(), roots.cend());
    while (!pending.isEmpty()) {
        const PackageId target = pending.takeLast();
        const auto edges = graph.incoming_of_type(target, EdgeType::Dependency);
        for (const ConstraintEdge& edge : edges) {
            if (!roots.contains(edge.source) && !dependents.contains(edge.source)) {
                dependents.insert(edge.source);
                pending.append(edge.source);
            }
        }
    }

    QStringList result;
    for (const QString& uuid : active) {
        if (excluded.contains(uuid)) {
            continue;
        }
        const checker::AboutXmlModPtr mod = about_mod(all_mods, uuid);
        if (mod && dependents.contains(mod->package_id)) {
            result.append(uuid);
        }
    }
    return result;
}

bool ActivationService::apply(const QStringList& enable, const QStringList& disable) {
    const QStringList& active = ctx_.activeUuids();
    const QSet<QString> disable_set(disable.cbegin(), disable.cend());
    QStringList new_active;
    for (const QString& uuid : active) {
        if (!disable_set.contains(uuid)) {
            new_active.append(uuid);
        }
    }
    const QSet<QString> active_set(active.cbegin(), active.cend());
    QStringList candidates =
        enable_candidates(ctx_.allMods(), enable, disable_set, active_set);

    const Placement placement(std::move(new_active),
                              std::move(candidates),
                              ctx_.allMods(),
                              ctx_.config().sort,
                              ctx_.diagnosticsService().community_rules());
    const QStringList result = placement.assemble();
    if (result == active) {
        return false;
    }

    ctx_.setActive(result);
    return true;
}

}  // namespace px::core
