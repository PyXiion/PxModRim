#pragma once

#include <QHash>
#include <QList>
#include <memory>

#include "core/checker/graph.hpp"
#include "core/checker/models.hpp"
#include "core/profiler.hpp"
#include "core/sort/config.hpp"
#include "core/sort/models.hpp"

namespace px::modrim::checker {

// Orders active mods by tier, then by dependencies and load-order constraints,
// breaking ties by the tier config priority and then by package id. Mods caught
// in a cycle are appended at the end of their tier in the same key order.
QList<PackageId> topologicalSort(
    const QHash<PackageId, std::shared_ptr<AboutXmlMod>>& activeMods,
    const ConstraintGraph& graph, const sort::SortSettings& settings,
    const sort::CommunityRules* communityRules = nullptr,
    Profiler* timer = nullptr);

}  // namespace px::modrim::checker
