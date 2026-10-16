#include "core/checker/graph.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace px::modrim::checker {
namespace {

constexpr std::size_t index_of_type(EdgeType type) {
    return static_cast<std::size_t>(type);
}

bool str_less(const PackageId& a, const PackageId& b) {
    return a.str() < b.str();
}

// Qt hashes are randomised per process, so the Tarjan traversal visits roots and neighbours
// in name order to keep the reported cycles stable from run to run.
QList<PackageId> sorted_pids(const QSet<PackageId>& pids) {
    QList<PackageId> sorted(pids.cbegin(), pids.cend());
    std::ranges::sort(sorted, str_less);
    return sorted;
}

QHash<PackageId, PackageId> build_alt_map(const QHash<PackageId, AboutXmlModPtr>& active_mods) {
    QHash<PackageId, PackageId> alt_map;
    for (const auto& mod : active_mods) {
        for (const DependencyMod& dep : mod->about_rules.dependencies) {
            if (active_mods.contains(dep.package_id)) {
                continue;
            }
            for (const PackageId& alternative_pid : dep.alternative_package_ids) {
                if (active_mods.contains(alternative_pid)) {
                    alt_map.insert(dep.package_id, alternative_pid);
                    break;
                }
            }
        }
    }
    return alt_map;
}

}  // namespace

QSet<ConstraintEdge> ConstraintGraph::outgoing(const PackageId& pid) const {
    QSet<ConstraintEdge> result;
    if (const auto it = outgoing_.constFind(pid); it != outgoing_.cend()) {
        for (const auto& edges : it.value()) {
            result.unite(edges);
        }
    }
    return result;
}

QSet<ConstraintEdge> ConstraintGraph::incoming(const PackageId& pid) const {
    QSet<ConstraintEdge> result;
    if (const auto it = incoming_.constFind(pid); it != incoming_.cend()) {
        for (const auto& edges : it.value()) {
            result.unite(edges);
        }
    }
    return result;
}

QSet<PackageId> ConstraintGraph::neighbors(const PackageId& pid) const {
    QSet<PackageId> result;
    if (const auto it = outgoing_.constFind(pid); it != outgoing_.cend()) {
        for (const auto& edges : it.value()) {
            for (const auto& edge : edges) {
                result.insert(edge.target);
            }
        }
    }
    if (const auto it = incoming_.constFind(pid); it != incoming_.cend()) {
        for (const auto& edges : it.value()) {
            for (const auto& edge : edges) {
                result.insert(edge.source);
            }
        }
    }
    return result;
}

QSet<ConstraintEdge> ConstraintGraph::edges_of_type(const PackageId& pid, EdgeType type) const {
    if (const auto it = outgoing_.constFind(pid); it != outgoing_.cend()) {
        return it.value()[index_of_type(type)];
    }
    return {};
}

QSet<ConstraintEdge> ConstraintGraph::incoming_of_type(const PackageId& pid, EdgeType type) const {
    if (const auto it = incoming_.constFind(pid); it != incoming_.cend()) {
        return it.value()[index_of_type(type)];
    }
    return {};
}

void ConstraintGraph::build(const QHash<PackageId, AboutXmlModPtr>& active_mods,
                            const QList<PackageId>& ordered_pids,
                            const sort::SortSettings& settings,
                            const sort::CommunityRules* community_rules) {
    outgoing_.clear();
    incoming_.clear();
    cycle_components_.reset();
    ordered_pids_ = ordered_pids;
    rebuild_index();

    // Pre-populate nodes so mods without any edges are still tracked.
    nodes_ = QSet<PackageId>(ordered_pids.cbegin(), ordered_pids.cend());

    const Alternatives alternatives =
        settings.use_alternative_package_ids ? build_alt_map(active_mods) : Alternatives{};

    for (auto it = active_mods.cbegin(); it != active_mods.cend(); ++it) {
        add_rules(it.key(), it.value()->about_rules, EdgeOrigin::AboutXml, alternatives);
    }

    if (settings.use_community_rules && community_rules != nullptr && !community_rules->isEmpty()) {
        for (auto it = active_mods.cbegin(); it != active_mods.cend(); ++it) {
            const auto rule = community_rules->constFind(it.key());
            if (rule == community_rules->cend()) {
                continue;
            }
            add_load_rules(it.key(),
                           rule->load_before,
                           rule->load_after,
                           rule->incompatible_with,
                           EdgeOrigin::CommunityRules,
                           alternatives);
        }
    }
}

void ConstraintGraph::remove_mod(const PackageId& pid) {
    cycle_components_.reset();
    const EdgesByType outgoing_edges = outgoing_.take(pid);
    for (const auto& edges : outgoing_edges) {
        for (const auto& edge : edges) {
            incoming_[edge.target][index_of_type(edge.type)].remove(edge);
        }
    }
    const EdgesByType incoming_edges = incoming_.take(pid);
    for (const auto& edges : incoming_edges) {
        for (const auto& edge : edges) {
            outgoing_[edge.source][index_of_type(edge.type)].remove(edge);
        }
    }
    nodes_.remove(pid);
    ordered_pids_.removeAll(pid);
    rebuild_index();
}

void ConstraintGraph::update_order(const QList<PackageId>& ordered_pids) {
    ordered_pids_ = ordered_pids;
    rebuild_index();
}

void ConstraintGraph::rebuild_index() {
    pid_to_index_.clear();
    for (qsizetype i = 0; i < ordered_pids_.size(); ++i) {
        pid_to_index_.insert(ordered_pids_[i], static_cast<int>(i));
    }
}

QList<QList<PackageId>> ConstraintGraph::find_cycles() {
    if (!cycle_components_) {
        cycle_components_ = find_cycle_components();
    }
    const auto missing = static_cast<int>(pid_to_index_.size());
    QList<QList<PackageId>> result;
    result.reserve(cycle_components_->size());
    for (QList<PackageId> component : *cycle_components_) {
        std::ranges::stable_sort(component, {}, [&](const PackageId& member) {
            return pid_to_index_.value(member, missing);
        });
        result.append(std::move(component));
    }
    return result;
}

QList<QList<PackageId>> ConstraintGraph::find_cycle_components() const {
    // Nodes without dependency or load-after edges are trivial components that can never be
    // part of a cycle, so they are left out of the search.
    QHash<PackageId, QList<PackageId>> adjacency;
    for (auto it = outgoing_.cbegin(); it != outgoing_.cend(); ++it) {
        QSet<PackageId> targets;
        for (const EdgeType type : {EdgeType::Dependency, EdgeType::LoadAfter}) {
            for (const auto& edge : it.value()[index_of_type(type)]) {
                targets.insert(edge.target);
            }
        }
        if (!targets.isEmpty()) {
            adjacency.insert(it.key(), sorted_pids(targets));
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
    std::vector<PackageId> active_stack;
    QSet<PackageId> on_stack;
    QList<QList<PackageId>> components;
    int next_index = 0;

    for (const PackageId& root : sorted_pids(QSet<PackageId>(adjacency.keyBegin(), adjacency.keyEnd()))) {
        if (indices.contains(root)) {
            continue;
        }

        indices.insert(root, next_index);
        lowlinks.insert(root, next_index);
        ++next_index;
        active_stack.push_back(root);
        on_stack.insert(root);
        std::vector<Frame> dfs_stack{Frame{root, &adjacency[root], 0}};

        while (!dfs_stack.empty()) {
            Frame& top = dfs_stack.back();
            const PackageId pid = top.pid;
            const QList<PackageId>& neighbors = *top.neighbors;

            if (top.next == neighbors.size()) {
                dfs_stack.pop_back();
                if (lowlinks[pid] == indices[pid]) {
                    QList<PackageId> component;
                    while (true) {
                        PackageId member = active_stack.back();
                        active_stack.pop_back();
                        on_stack.remove(member);
                        const bool is_root = member == pid;
                        component.append(std::move(member));
                        if (is_root) {
                            break;
                        }
                    }
                    if (component.size() > 1 || adjacency[pid].contains(pid)) {
                        components.append(std::move(component));
                    }
                }
                if (!dfs_stack.empty()) {
                    const PackageId& parent = dfs_stack.back().pid;
                    lowlinks[parent] = std::min(lowlinks[parent], lowlinks[pid]);
                }
                continue;
            }

            const PackageId neighbor = neighbors[top.next++];
            if (!indices.contains(neighbor)) {
                indices.insert(neighbor, next_index);
                lowlinks.insert(neighbor, next_index);
                ++next_index;
                active_stack.push_back(neighbor);
                on_stack.insert(neighbor);
                dfs_stack.push_back(Frame{neighbor, &adjacency[neighbor], 0});
            } else if (on_stack.contains(neighbor) && indices[neighbor] < lowlinks[pid]) {
                lowlinks[pid] = indices[neighbor];
            }
        }
    }

    return components;
}

template <typename Targets>
void ConstraintGraph::add_edges(const PackageId& source,
                                const Targets& targets,
                                EdgeType type,
                                EdgeOrigin origin,
                                const Alternatives& alternatives) {
    // Hot path of build(): the per-source edge set is looked up once, not per target.
    QSet<ConstraintEdge>* out_edges = nullptr;
    for (const PackageId& raw_target : targets) {
        const PackageId target = alternatives.value(raw_target, raw_target);
        if (out_edges == nullptr) {
            nodes_.insert(source);
            out_edges = &outgoing_[source][index_of_type(type)];
        }
        nodes_.insert(target);
        const ConstraintEdge edge{source, target, type, origin};
        out_edges->insert(edge);
        incoming_[target][index_of_type(type)].insert(edge);
    }
}

void ConstraintGraph::add_rules(const PackageId& pid,
                                const BaseRules& rules,
                                EdgeOrigin origin,
                                const Alternatives& alternatives) {
    for (const DependencyMod& dep : rules.dependencies) {
        add_edges(pid, std::array{dep.package_id}, EdgeType::Dependency, origin, alternatives);
        if (!dep.alternative_package_ids.empty()) {
            add_edges(pid, dep.alternative_package_ids, EdgeType::Alternative, origin, {});
        }
    }

    add_load_rules(pid,
                   rules.load_before,
                   rules.load_after,
                   rules.incompatible_with,
                   origin,
                   alternatives);
}

void ConstraintGraph::add_load_rules(const PackageId& pid,
                                     const CaseInsensitiveSet& load_before,
                                     const CaseInsensitiveSet& load_after,
                                     const CaseInsensitiveSet& incompatible_with,
                                     EdgeOrigin origin,
                                     const Alternatives& alternatives) {
    if (!load_after.empty()) {
        add_edges(pid, load_after, EdgeType::LoadAfter, origin, alternatives);
    }
    if (!load_before.empty()) {
        add_edges(pid, load_before, EdgeType::LoadBefore, origin, alternatives);
    }
    if (!incompatible_with.empty()) {
        add_edges(pid, incompatible_with, EdgeType::Incompatibility, origin, alternatives);
    }
}

}  // namespace px::modrim::checker
