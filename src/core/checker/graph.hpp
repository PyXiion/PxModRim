#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <array>
#include <cstdint>
#include <optional>

#include "core/checker/models.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"
#include "core/sort/models.hpp"

namespace px::modrim::checker {

enum class EdgeType : std::uint8_t {
  kDependency,
  kLoadAfter,
  kLoadBefore,
  kIncompatibility,
  kAlternative,
};

inline constexpr std::size_t kEdgeTypeCount = 5;

enum class EdgeOrigin : std::uint8_t {
  kAboutXml,
  kCommunityRules,
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

// Directed graph of mod dependencies, load-order rules and incompatibilities.
// Nodes are the ordered mods plus any package id a rule points at, whether or
// not it is active.
class ConstraintGraph {
 public:
  using Alternatives = QHash<PackageId, PackageId>;

  const QSet<PackageId>& nodes() const noexcept { return m_nodes; }

  // Union over all edge types. Returned by value: Qt containers are implicitly
  // shared.
  QSet<ConstraintEdge> outgoing(const PackageId& pid) const;
  QSet<ConstraintEdge> incoming(const PackageId& pid) const;
  QSet<ConstraintEdge> edgesOfType(const PackageId& pid, EdgeType type) const;
  QSet<ConstraintEdge> incomingOfType(const PackageId& pid,
                                      EdgeType type) const;

  const QHash<PackageId, int>& pidToIndex() const noexcept {
    return m_pidToIndex;
  }

  // Replaces all edges. A null or empty `community_rules` means no community
  // rules.
  void build(const QHash<PackageId, AboutXmlModPtr>& activeMods,
             const QList<PackageId>& orderedPids,
             const sort::SortSettings& settings,
             const sort::CommunityRules* communityRules = nullptr);

  // Replaces the mod ordering without touching edges.
  void updateOrder(const QList<PackageId>& orderedPids);

  // Strongly connected components that contain a dependency or load-after
  // cycle. Members are sorted by load index. Cached until the edges change.
  QList<QList<PackageId>> findCycles();

 private:
  using EdgesByType = std::array<QSet<ConstraintEdge>, kEdgeTypeCount>;

  template <typename Targets>
  void addEdges(const PackageId& source, const Targets& targets, EdgeType type,
                EdgeOrigin origin, const Alternatives& alternatives);
  void addRules(const PackageId& pid, const BaseRules& rules, EdgeOrigin origin,
                const Alternatives& alternatives,
                const QHash<PackageId, AboutXmlModPtr>& activeMods,
                bool useAlternativePackageIds);
  void addLoadRules(const PackageId& pid, const CaseInsensitiveSet& loadBefore,
                    const CaseInsensitiveSet& loadAfter,
                    const CaseInsensitiveSet& incompatibleWith,
                    EdgeOrigin origin, const Alternatives& alternatives);
  void rebuildIndex();
  QList<QList<PackageId>> findCycleComponents() const;

  QHash<PackageId, EdgesByType> m_outgoing;
  QHash<PackageId, EdgesByType> m_incoming;
  QSet<PackageId> m_nodes;
  QHash<PackageId, int> m_pidToIndex;
  QList<PackageId> m_orderedPids;
  // Cycle membership depends only on edges, so it survives reorders.
  std::optional<QList<QList<PackageId>>> m_cycleComponents;
};

}  // namespace px::modrim::checker
