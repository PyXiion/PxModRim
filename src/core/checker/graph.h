#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <QHash>
#include <QList>
#include <QSet>

#include "core/checker/models.h"
#include "core/models/metadata/structures.h"
#include "core/sort/config.h"
#include "core/sort/models.h"

namespace px::core::checker {

enum class EdgeType : std::uint8_t {
    Dependency,
    LoadAfter,
    LoadBefore,
    Incompatibility,
    Alternative,
};

inline constexpr std::size_t kEdgeTypeCount = 5;

enum class EdgeOrigin : std::uint8_t {
    AboutXml,
    CommunityRules,
};

struct ConstraintEdge {
    PackageId source;
    PackageId target;
    EdgeType type;
    EdgeOrigin origin;

    bool operator==(const ConstraintEdge&) const = default;

    friend size_t qHash(const ConstraintEdge& edge, size_t seed = 0) noexcept {
        return qHashMulti(seed, edge.source, edge.target, edge.type, edge.origin);
    }
};

// Directed graph of mod dependencies, load-order rules and incompatibilities. Nodes are the
// ordered mods plus any package id a rule points at, whether or not it is active.
class ConstraintGraph {
public:
    using Alternatives = QHash<PackageId, PackageId>;

    const QSet<PackageId>& nodes() const noexcept { return nodes_; }
    bool has_node(const PackageId& pid) const { return nodes_.contains(pid); }

    // Union over all edge types. Returned by value: Qt containers are implicitly shared.
    QSet<ConstraintEdge> outgoing(const PackageId& pid) const;
    QSet<ConstraintEdge> incoming(const PackageId& pid) const;
    QSet<PackageId> neighbors(const PackageId& pid) const;
    QSet<ConstraintEdge> edges_of_type(const PackageId& pid, EdgeType type) const;
    QSet<ConstraintEdge> incoming_of_type(const PackageId& pid, EdgeType type) const;

    int index_of(const PackageId& pid) const { return pid_to_index_.value(pid, -1); }
    const QHash<PackageId, int>& pid_to_index() const noexcept { return pid_to_index_; }

    // Replaces all edges. A null or empty `community_rules` means no community rules.
    void build(const QHash<PackageId, AboutXmlModPtr>& active_mods,
               const QList<PackageId>& ordered_pids,
               const sort::SortSettings& settings,
               const sort::CommunityRules* community_rules = nullptr);

    void remove_mod(const PackageId& pid);

    // Replaces the mod ordering without touching edges.
    void update_order(const QList<PackageId>& ordered_pids);

    // Strongly connected components that contain a dependency or load-after cycle. Members are
    // sorted by load index. Cached until the edges change.
    QList<QList<PackageId>> find_cycles();

private:
    using EdgesByType = std::array<QSet<ConstraintEdge>, kEdgeTypeCount>;

    template <typename Targets>
    void add_edges(const PackageId& source,
                   const Targets& targets,
                   EdgeType type,
                   EdgeOrigin origin,
                   const Alternatives& alternatives);
    void add_rules(const PackageId& pid,
                   const BaseRules& rules,
                   EdgeOrigin origin,
                   const Alternatives& alternatives);
    void add_load_rules(const PackageId& pid,
                        const CaseInsensitiveSet& load_before,
                        const CaseInsensitiveSet& load_after,
                        const CaseInsensitiveSet& incompatible_with,
                        EdgeOrigin origin,
                        const Alternatives& alternatives);
    void rebuild_index();
    QList<QList<PackageId>> find_cycle_components() const;

    QHash<PackageId, EdgesByType> outgoing_;
    QHash<PackageId, EdgesByType> incoming_;
    QSet<PackageId> nodes_;
    QHash<PackageId, int> pid_to_index_;
    QList<PackageId> ordered_pids_;
    // Cycle membership depends only on edges, so it survives reorders.
    std::optional<QList<QList<PackageId>>> cycle_components_;
};

}  // namespace px::core::checker
