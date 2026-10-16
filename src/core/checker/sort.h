#pragma once

#include <memory>

#include <QHash>
#include <QList>

#include "core/checker/graph.h"
#include "core/checker/models.h"
#include "core/profiler.h"
#include "core/sort/config.h"
#include "core/sort/models.h"

namespace px::modrim::checker {

// Orders active mods by tier, then by dependencies and load-order constraints, breaking ties by
// the tier config priority and then by package id. Mods caught in a cycle are appended at the
// end of their tier in the same key order.
QList<PackageId> topological_sort(const QHash<PackageId, std::shared_ptr<AboutXmlMod>>& active_mods,
                                  const ConstraintGraph& graph,
                                  const sort::SortSettings& settings,
                                  const sort::CommunityRules* community_rules = nullptr,
                                  Profiler* timer = nullptr);

}  // namespace px::modrim::checker
