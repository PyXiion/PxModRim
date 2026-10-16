#include "core/services/activation_service.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

#include <QHash>
#include <QSet>

#include "core/checker/graph.hpp"
#include "core/context.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/services/diagnostics_service.hpp"
#include "core/sort/config.hpp"
#include "core/sort/models.hpp"

namespace px::modrim {
namespace {

using checker::ConstraintEdge;
using checker::ConstraintGraph;
using checker::EdgeType;
using checker::PackageId;
using ModMap = QHash<PackageId, checker::AboutXmlModPtr>;
using Edge = std::pair<PackageId, PackageId>;

// The About.xml mod behind `uuid`, or null when it is unknown or has no About.xml.
checker::AboutXmlModPtr aboutMod(const QHash<QString, ListedModPtr>& allMods, const QString& uuid) {
    const auto it = allMods.constFind(uuid);
    return it == allMods.cend() ? nullptr : std::dynamic_pointer_cast<AboutXmlMod>(it.value());
}

QStringList enableCandidates(const QHash<QString, ListedModPtr>& allMods,
                              const QStringList& enable,
                              const QSet<QString>& disableSet,
                              const QSet<QString>& activeSet) {
    QStringList candidates;
    QSet<QString> seen;
    for (const QString& uuid : enable) {
        if (seen.contains(uuid) || disableSet.contains(uuid)) {
            continue;
        }
        seen.insert(uuid);
        if (allMods.contains(uuid) && !activeSet.contains(uuid)) {
            candidates.append(uuid);
        }
    }
    return candidates;
}

// (before, after) pairs between mods touching any of `candidates`. Edges through ids outside
// `mods` (uninstalled optional mods) are dropped so they cannot chain unrelated mods together.
QList<Edge> buildEdges(const ModMap& mods,
                        const QList<PackageId>& order,
                        const QList<PackageId>& candidates,
                        const sort::SortSettings& settings,
                        const sort::CommunityRules& communityRules) {
    ConstraintGraph graph;
    graph.build(mods, order, settings, &communityRules);
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
        case EdgeType::kLoadBefore:
            before = edge.source;
            after = edge.target;
            break;
        case EdgeType::kDependency:
        case EdgeType::kLoadAfter:
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
QList<QStringList> stronglyConnected(const QStringList& nodes,
                                      const QHash<QString, QSet<QString>>& adjacent) {
    struct Frame {
        QString node;
        QStringList children;
        qsizetype next = 0;
    };

    QHash<QString, int> indexOf;
    QHash<QString, int> lowlink;
    QStringList stack;
    QSet<QString> onStack;
    QList<QStringList> components;
    QList<Frame> work;

    const auto visit = [&](const QString& node) {
        const int index = static_cast<int>(indexOf.size());
        indexOf.insert(node, index);
        lowlink.insert(node, index);
        stack.append(node);
        onStack.insert(node);
        const QSet<QString> successors = adjacent.value(node);
        work.append(Frame{node, QStringList(successors.cbegin(), successors.cend()), 0});
    };

    for (const QString& root : nodes) {
        if (indexOf.contains(root)) {
            continue;
        }
        visit(root);
        while (!work.isEmpty()) {
            Frame& frame = work.last();
            if (frame.next < frame.children.size()) {
                const QString child = frame.children[frame.next++];
                if (!indexOf.contains(child)) {
                    // Invalidates `frame`; the loop re-reads work.last().
                    visit(child);
                } else if (onStack.contains(child)) {
                    lowlink[frame.node] = std::min(lowlink.value(frame.node), indexOf.value(child));
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
            if (lowlink.value(node) == indexOf.value(node)) {
                QStringList component;
                QString member;
                do {
                    member = stack.takeLast();
                    onStack.remove(member);
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
    Placement(QStringList newActive,
              QStringList candidates,
              const QHash<QString, ListedModPtr>& allMods,
              const sort::SortSettings& settings,
              const sort::CommunityRules& communityRules);

    QStringList assemble() const;

private:
    void addEdge(const PackageId& before, const PackageId& after);
    std::pair<QStringList, QSet<QString>> orderBatch() const;
    QHash<QString, int> propagatedCeilings(const QStringList& ordered,
                                            const QSet<QString>& cyclic) const;
    int gapFor(const QString& uuid,
                const QSet<QString>& cyclic,
                const QHash<QString, int>& ceilings,
                const QHash<QString, int>& gaps) const;

    QStringList m_newActive;
    QStringList m_candidates;
    int m_end;

    QHash<PackageId, int> m_firstPos;
    QHash<PackageId, int> m_lastPos;
    QHash<PackageId, QStringList> m_uuidsByPid;

    // Batch-internal adjacency plus direct bounds against active mods.
    QHash<QString, QSet<QString>> m_after;
    QHash<QString, QSet<QString>> m_before;
    QHash<QString, int> m_floor;
    QHash<QString, int> m_ceiling;
};

Placement::Placement(QStringList newActive,
                     QStringList candidates,
                     const QHash<QString, ListedModPtr>& allMods,
                     const sort::SortSettings& settings,
                     const sort::CommunityRules& communityRules)
    : m_newActive(std::move(newActive)),
      m_candidates(std::move(candidates)),
      m_end(static_cast<int>(m_newActive.size())) {
    ModMap mods;
    QList<PackageId> order;
    for (qsizetype index = 0; index < m_newActive.size(); ++index) {
        const checker::AboutXmlModPtr mod = aboutMod(allMods, m_newActive[index]);
        if (!mod) {
            continue;
        }
        const PackageId& pid = mod->packageId;
        if (!m_firstPos.contains(pid)) {
            m_firstPos.insert(pid, static_cast<int>(index));
        }
        m_lastPos.insert(pid, static_cast<int>(index));
        if (!mods.contains(pid)) {
            mods.insert(pid, mod);
            order.append(pid);
        }
    }

    for (const QString& uuid : m_candidates) {
        m_after.insert(uuid, {});
        m_before.insert(uuid, {});
        const checker::AboutXmlModPtr mod = aboutMod(allMods, uuid);
        if (!mod) {
            continue;
        }
        const PackageId& pid = mod->packageId;
        m_uuidsByPid[pid].append(uuid);
        if (!mods.contains(pid)) {
            mods.insert(pid, mod);
            order.append(pid);
        }
    }

    if (!m_uuidsByPid.isEmpty()) {
        for (const Edge& edge :
             buildEdges(mods, order, m_uuidsByPid.keys(), settings, communityRules)) {
            addEdge(edge.first, edge.second);
        }
    }
}

void Placement::addEdge(const PackageId& before, const PackageId& after) {
    const QStringList beforeUuids = m_uuidsByPid.value(before);
    const QStringList afterUuids = m_uuidsByPid.value(after);
    for (const QString& b : beforeUuids) {
        for (const QString& a : afterUuids) {
            if (a != b) {
                m_after[b].insert(a);
                m_before[a].insert(b);
            }
        }
        if (const auto first = m_firstPos.constFind(after); first != m_firstPos.cend()) {
            m_ceiling[b] = std::min(m_ceiling.value(b, first.value()), first.value());
        }
    }
    if (const auto last = m_lastPos.constFind(before); last != m_lastPos.cend()) {
        for (const QString& a : afterUuids) {
            m_floor[a] = std::max(m_floor.value(a, last.value() + 1), last.value() + 1);
        }
    }
}

QStringList Placement::assemble() const {
    const auto [ordered, cyclic] = orderBatch();
    const QHash<QString, int> ceilings = propagatedCeilings(ordered, cyclic);
    QList<QStringList> buckets(m_end + 1);
    QHash<QString, int> gaps;
    for (const QString& uuid : ordered) {
        const int gap = gapFor(uuid, cyclic, ceilings, gaps);
        gaps.insert(uuid, gap);
        buckets[gap].append(uuid);
    }

    QStringList result;
    for (qsizetype index = 0; index < m_newActive.size(); ++index) {
        result.append(buckets[index]);
        result.append(m_newActive[index]);
    }
    result.append(buckets[m_end]);
    return result;
}

int Placement::gapFor(const QString& uuid,
                       const QSet<QString>& cyclic,
                       const QHash<QString, int>& ceilings,
                       const QHash<QString, int>& gaps) const {
    if (cyclic.contains(uuid)) {
        return m_end;
    }
    // Befores of an acyclic candidate sit in earlier components, so their gaps are final.
    std::optional<int> earliest;
    const auto raiseFloor = [&earliest](int floor) {
        earliest = earliest ? std::max(*earliest, floor) : floor;
    };
    for (const QString& b : m_before.value(uuid)) {
        raiseFloor(gaps.value(b));
    }
    if (const auto floor = m_floor.constFind(uuid); floor != m_floor.cend()) {
        raiseFloor(floor.value());
    }

    std::optional<int> ceiling;
    if (const auto found = ceilings.constFind(uuid); found != ceilings.cend()) {
        ceiling = found.value();
    }
    if (earliest) {
        if (ceiling && *earliest > *ceiling) {
            return m_end;
        }
        return *earliest;
    }
    return ceiling ? *ceiling : m_end;
}

QHash<QString, int> Placement::propagatedCeilings(const QStringList& ordered,
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
        for (const QString& a : m_after.value(*it)) {
            if (const auto found = ceilings.constFind(a); found != ceilings.cend()) {
                tighten(found.value());
            }
        }
        if (const auto own = m_ceiling.constFind(*it); own != m_ceiling.cend()) {
            tighten(own.value());
        }
        if (bound) {
            ceilings.insert(*it, *bound);
        }
    }
    return ceilings;
}

// Topologically orders candidates by input order; cycle members are placed as a unit.
std::pair<QStringList, QSet<QString>> Placement::orderBatch() const {
    auto components = stronglyConnected(m_candidates, m_after);

    QHash<QString, int> inputIndex;
    for (qsizetype index = 0; index < m_candidates.size(); ++index) {
        inputIndex.insert(m_candidates[index], static_cast<int>(index));
    }

    QHash<QString, int> componentOf;
    QSet<QString> cyclic;
    for (int index = 0; index < components.size(); ++index) {
        QStringList& members = components[index];
        std::sort(members.begin(), members.end(), [&inputIndex](const QString& a, const QString& b) {
            return inputIndex.value(a) < inputIndex.value(b);
        });
        for (const QString& member : members) {
            componentOf.insert(member, index);
        }
        if (members.size() > 1) {
            cyclic.unite(QSet<QString>(members.cbegin(), members.cend()));
        }
    }

    QList<QSet<int>> successors(components.size());
    QList<int> indegree(components.size(), 0);
    for (auto it = m_after.cbegin(); it != m_after.cend(); ++it) {
        const int source = componentOf.value(it.key());
        for (const QString& successor : it.value()) {
            const int target = componentOf.value(successor);
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
            ready.emplace(inputIndex.value(components[index].front()), index);
        }
    }
    QStringList ordered;
    while (!ready.empty()) {
        const int index = ready.top().second;
        ready.pop();
        ordered.append(components[index]);
        for (const int target : successors[index]) {
            if (--indegree[target] == 0) {
                ready.emplace(inputIndex.value(components[target].front()), target);
            }
        }
    }
    return {std::move(ordered), std::move(cyclic)};
}

}  // namespace

ActivationService::ActivationService(CoreContext& ctx) : m_ctx(ctx) {}

QStringList ActivationService::dependentsOf(const QStringList& uuids,
                                             const QStringList& enable) const {
    const QSet<QString> excluded(uuids.cbegin(), uuids.cend());
    const QHash<QString, ListedModPtr>& allMods = m_ctx.allMods();
    const QStringList& active = m_ctx.activeUuids();

    QSet<PackageId> remaining;
    const auto keep = [&](const QString& uuid) {
        if (excluded.contains(uuid)) {
            return;
        }
        if (const checker::AboutXmlModPtr mod = aboutMod(allMods, uuid)) {
            remaining.insert(mod->packageId);
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
        const checker::AboutXmlModPtr mod = aboutMod(allMods, uuid);
        if (mod && !remaining.contains(mod->packageId)) {
            roots.insert(mod->packageId);
        }
    }
    if (roots.isEmpty()) {
        return {};
    }

    const ConstraintGraph& graph = m_ctx.diagnosticsService().constraintGraph();
    QSet<PackageId> dependents;
    QList<PackageId> pending(roots.cbegin(), roots.cend());
    while (!pending.isEmpty()) {
        const PackageId target = pending.takeLast();
        const auto edges = graph.incomingOfType(target, EdgeType::kDependency);
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
        const checker::AboutXmlModPtr mod = aboutMod(allMods, uuid);
        if (mod && dependents.contains(mod->packageId)) {
            result.append(uuid);
        }
    }
    return result;
}

bool ActivationService::apply(const QStringList& enable, const QStringList& disable) {
    const QStringList& active = m_ctx.activeUuids();
    const QSet<QString> disableSet(disable.cbegin(), disable.cend());
    QStringList newActive;
    for (const QString& uuid : active) {
        if (!disableSet.contains(uuid)) {
            newActive.append(uuid);
        }
    }
    const QSet<QString> activeSet(active.cbegin(), active.cend());
    QStringList candidates =
        enableCandidates(m_ctx.allMods(), enable, disableSet, activeSet);

    const Placement placement(std::move(newActive),
                              std::move(candidates),
                              m_ctx.allMods(),
                              m_ctx.config().sort,
                              m_ctx.diagnosticsService().communityRules());
    const QStringList result = placement.assemble();
    if (result == active) {
        return false;
    }

    m_ctx.setActive(result);
    return true;
}

}  // namespace px::modrim
