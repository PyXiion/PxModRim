#include "core/checker/graph.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace px::modrim::checker {
namespace {

constexpr std::size_t indexOfType(EdgeType type) {
    return static_cast<std::size_t>(type);
}

bool strLess(const PackageId& a, const PackageId& b) {
    return a.str() < b.str();
}

// Qt hashes are randomised per process, so the Tarjan traversal visits roots and neighbours
// in name order to keep the reported cycles stable from run to run.
QList<PackageId> sortedPids(const QSet<PackageId>& pids) {
    QList<PackageId> sorted(pids.cbegin(), pids.cend());
    std::ranges::sort(sorted, strLess);
    return sorted;
}

QHash<PackageId, PackageId> buildAltMap(const QHash<PackageId, AboutXmlModPtr>& activeMods) {
    QHash<PackageId, PackageId> altMap;
    for (const auto& mod : activeMods) {
        for (const DependencyMod& dep : mod->aboutRules.dependencies) {
            if (activeMods.contains(dep.packageId)) {
                continue;
            }
            for (const PackageId& alternativePid : dep.alternativePackageIds) {
                if (activeMods.contains(alternativePid)) {
                    altMap.insert(dep.packageId, alternativePid);
                    break;
                }
            }
        }
    }
    return altMap;
}

}  // namespace

QSet<ConstraintEdge> ConstraintGraph::outgoing(const PackageId& pid) const {
    QSet<ConstraintEdge> result;
    if (const auto it = m_outgoing.constFind(pid); it != m_outgoing.cend()) {
        for (const auto& edges : it.value()) {
            result.unite(edges);
        }
    }
    return result;
}

QSet<ConstraintEdge> ConstraintGraph::incoming(const PackageId& pid) const {
    QSet<ConstraintEdge> result;
    if (const auto it = m_incoming.constFind(pid); it != m_incoming.cend()) {
        for (const auto& edges : it.value()) {
            result.unite(edges);
        }
    }
    return result;
}

QSet<PackageId> ConstraintGraph::neighbors(const PackageId& pid) const {
    QSet<PackageId> result;
    if (const auto it = m_outgoing.constFind(pid); it != m_outgoing.cend()) {
        for (const auto& edges : it.value()) {
            for (const auto& edge : edges) {
                result.insert(edge.target);
            }
        }
    }
    if (const auto it = m_incoming.constFind(pid); it != m_incoming.cend()) {
        for (const auto& edges : it.value()) {
            for (const auto& edge : edges) {
                result.insert(edge.source);
            }
        }
    }
    return result;
}

QSet<ConstraintEdge> ConstraintGraph::edgesOfType(const PackageId& pid, EdgeType type) const {
    if (const auto it = m_outgoing.constFind(pid); it != m_outgoing.cend()) {
        return it.value()[indexOfType(type)];
    }
    return {};
}

QSet<ConstraintEdge> ConstraintGraph::incomingOfType(const PackageId& pid, EdgeType type) const {
    if (const auto it = m_incoming.constFind(pid); it != m_incoming.cend()) {
        return it.value()[indexOfType(type)];
    }
    return {};
}

void ConstraintGraph::build(const QHash<PackageId, AboutXmlModPtr>& activeMods,
                            const QList<PackageId>& orderedPids,
                            const sort::SortSettings& settings,
                            const sort::CommunityRules* communityRules) {
    m_outgoing.clear();
    m_incoming.clear();
    m_cycleComponents.reset();
    m_orderedPids = orderedPids;
    rebuildIndex();

    // Pre-populate nodes so mods without any edges are still tracked.
    m_nodes = QSet<PackageId>(orderedPids.cbegin(), orderedPids.cend());

    const Alternatives alternatives =
        settings.use_alternative_package_ids ? buildAltMap(activeMods) : Alternatives{};

    for (auto it = activeMods.cbegin(); it != activeMods.cend(); ++it) {
        addRules(it.key(), it.value()->aboutRules, EdgeOrigin::kAboutXml, alternatives);
    }

    if (settings.use_community_rules && communityRules != nullptr && !communityRules->isEmpty()) {
        for (auto it = activeMods.cbegin(); it != activeMods.cend(); ++it) {
            const auto rule = communityRules->constFind(it.key());
            if (rule == communityRules->cend()) {
                continue;
            }
            addLoadRules(it.key(),
                           rule->loadBefore,
                           rule->loadAfter,
                           rule->incompatibleWith,
                           EdgeOrigin::kCommunityRules,
                           alternatives);
        }
    }
}

void ConstraintGraph::removeMod(const PackageId& pid) {
    m_cycleComponents.reset();
    const EdgesByType outgoingEdges = m_outgoing.take(pid);
    for (const auto& edges : outgoingEdges) {
        for (const auto& edge : edges) {
            m_incoming[edge.target][indexOfType(edge.type)].remove(edge);
        }
    }
    const EdgesByType incomingEdges = m_incoming.take(pid);
    for (const auto& edges : incomingEdges) {
        for (const auto& edge : edges) {
            m_outgoing[edge.source][indexOfType(edge.type)].remove(edge);
        }
    }
    m_nodes.remove(pid);
    m_orderedPids.removeAll(pid);
    rebuildIndex();
}

void ConstraintGraph::updateOrder(const QList<PackageId>& orderedPids) {
    m_orderedPids = orderedPids;
    rebuildIndex();
}

void ConstraintGraph::rebuildIndex() {
    m_pidToIndex.clear();
    for (qsizetype i = 0; i < m_orderedPids.size(); ++i) {
        m_pidToIndex.insert(m_orderedPids[i], static_cast<int>(i));
    }
}

QList<QList<PackageId>> ConstraintGraph::findCycles() {
    if (!m_cycleComponents) {
        m_cycleComponents = findCycleComponents();
    }
    const auto missing = static_cast<int>(m_pidToIndex.size());
    QList<QList<PackageId>> result;
    result.reserve(m_cycleComponents->size());
    for (QList<PackageId> component : *m_cycleComponents) {
        std::ranges::stable_sort(component, {}, [&](const PackageId& member) {
            return m_pidToIndex.value(member, missing);
        });
        result.append(std::move(component));
    }
    return result;
}

QList<QList<PackageId>> ConstraintGraph::findCycleComponents() const {
    // Nodes without dependency or load-after edges are trivial components that can never be
    // part of a cycle, so they are left out of the search.
    QHash<PackageId, QList<PackageId>> adjacency;
    for (auto it = m_outgoing.cbegin(); it != m_outgoing.cend(); ++it) {
        QSet<PackageId> targets;
        for (const EdgeType type : {EdgeType::kDependency, EdgeType::kLoadAfter}) {
            for (const auto& edge : it.value()[indexOfType(type)]) {
                targets.insert(edge.target);
            }
        }
        if (!targets.isEmpty()) {
            adjacency.insert(it.key(), sortedPids(targets));
        }
    }
    for (auto& neighbors : adjacency) {
        neighbors.removeIf([&](const PackageId& target) { return !adjacency.contains(target); });
    }

    struct Frame {
        PackageId pid;
        const QList<PackageId>* neighbors;
        qsizetype next;
    };

    QHash<PackageId, int> indices;
    QHash<PackageId, int> lowlinks;
    std::vector<PackageId> activeStack;
    QSet<PackageId> onStack;
    QList<QList<PackageId>> components;
    int nextIndex = 0;

    for (const PackageId& root : sortedPids(QSet<PackageId>(adjacency.keyBegin(), adjacency.keyEnd()))) {
        if (indices.contains(root)) {
            continue;
        }

        indices.insert(root, nextIndex);
        lowlinks.insert(root, nextIndex);
        ++nextIndex;
        activeStack.push_back(root);
        onStack.insert(root);
        std::vector<Frame> dfsStack{Frame{root, &adjacency[root], 0}};

        while (!dfsStack.empty()) {
            Frame& top = dfsStack.back();
            const PackageId pid = top.pid;
            const QList<PackageId>& neighbors = *top.neighbors;

            if (top.next == neighbors.size()) {
                dfsStack.pop_back();
                if (lowlinks[pid] == indices[pid]) {
                    QList<PackageId> component;
                    while (true) {
                        PackageId member = activeStack.back();
                        activeStack.pop_back();
                        onStack.remove(member);
                        const bool isRoot = member == pid;
                        component.append(std::move(member));
                        if (isRoot) {
                            break;
                        }
                    }
                    if (component.size() > 1 || adjacency[pid].contains(pid)) {
                        components.append(std::move(component));
                    }
                }
                if (!dfsStack.empty()) {
                    const PackageId& parent = dfsStack.back().pid;
                    lowlinks[parent] = std::min(lowlinks[parent], lowlinks[pid]);
                }
                continue;
            }

            const PackageId neighbor = neighbors[top.next++];
            if (!indices.contains(neighbor)) {
                indices.insert(neighbor, nextIndex);
                lowlinks.insert(neighbor, nextIndex);
                ++nextIndex;
                activeStack.push_back(neighbor);
                onStack.insert(neighbor);
                dfsStack.push_back(Frame{neighbor, &adjacency[neighbor], 0});
            } else if (onStack.contains(neighbor) && indices[neighbor] < lowlinks[pid]) {
                lowlinks[pid] = indices[neighbor];
            }
        }
    }

    return components;
}

template <typename Targets>
void ConstraintGraph::addEdges(const PackageId& source,
                                const Targets& targets,
                                EdgeType type,
                                EdgeOrigin origin,
                                const Alternatives& alternatives) {
    // Hot path of build(): the per-source edge set is looked up once, not per target.
    QSet<ConstraintEdge>* outEdges = nullptr;
    for (const PackageId& rawTarget : targets) {
        const PackageId target = alternatives.value(rawTarget, rawTarget);
        if (outEdges == nullptr) {
            m_nodes.insert(source);
            outEdges = &m_outgoing[source][indexOfType(type)];
        }
        m_nodes.insert(target);
        const ConstraintEdge edge{source, target, type, origin};
        outEdges->insert(edge);
        m_incoming[target][indexOfType(type)].insert(edge);
    }
}

void ConstraintGraph::addRules(const PackageId& pid,
                                const BaseRules& rules,
                                EdgeOrigin origin,
                                const Alternatives& alternatives) {
    for (const DependencyMod& dep : rules.dependencies) {
        addEdges(pid, std::array{dep.packageId}, EdgeType::kDependency, origin, alternatives);
        if (!dep.alternativePackageIds.empty()) {
            addEdges(pid, dep.alternativePackageIds, EdgeType::kAlternative, origin, {});
        }
    }

    addLoadRules(pid,
                   rules.loadBefore,
                   rules.loadAfter,
                   rules.incompatibleWith,
                   origin,
                   alternatives);
}

void ConstraintGraph::addLoadRules(const PackageId& pid,
                                     const CaseInsensitiveSet& loadBefore,
                                     const CaseInsensitiveSet& loadAfter,
                                     const CaseInsensitiveSet& incompatibleWith,
                                     EdgeOrigin origin,
                                     const Alternatives& alternatives) {
    if (!loadAfter.empty()) {
        addEdges(pid, loadAfter, EdgeType::kLoadAfter, origin, alternatives);
    }
    if (!loadBefore.empty()) {
        addEdges(pid, loadBefore, EdgeType::kLoadBefore, origin, alternatives);
    }
    if (!incompatibleWith.empty()) {
        addEdges(pid, incompatibleWith, EdgeType::kIncompatibility, origin, alternatives);
    }
}

}  // namespace px::modrim::checker
