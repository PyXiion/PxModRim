#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <optional>

#include <QCoro/QCoroTask>

#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

namespace px::modrim {

class CoreContext;
class DiagnosticsService;

class SortService {
 public:
  // Neither argument is owned; both must outlive the service.
  SortService(CoreContext& ctx, DiagnosticsService& diagnostics);

  // Orders the active mods by tier, dependencies and load-order rules. Resolves
  // to their uuids in that order, or to nullopt when any input changed while
  // the worker ran, so a stale order never overwrites newer state.
  QCoro::Task<std::optional<QStringList>> sortActiveMods();

  // Uuids of inactive mods that the active mods need and that are not already
  // covered.
  QStringList resolveMissingDependencies(
      const QSet<QString>& activeUuids) const;

  // Maps topologically sorted package ids back to the uuids of the active mods
  // that declare them. Only the active uuid of a package id is considered, so
  // an inactive duplicate never stands in for an active mod.
  static QStringList orderUuids(const QList<sort::PackageId>& sortedPids,
                                const QHash<QString, ListedModPtr>& allMods,
                                const QStringList& activeUuids);

  // Uuids of inactive mods that satisfy a dependency of an active mod which no
  // active mod (or alternative package id) already covers. Duplicates are
  // removed, keeping first occurrence.
  static QStringList missingDependencyUuids(
      const QHash<QString, ListedModPtr>& allMods,
      const QSet<QString>& activeUuids);

 private:
  CoreContext& m_ctx;
  DiagnosticsService& m_diagnostics;
};

}  // namespace px::modrim
