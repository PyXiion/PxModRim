#pragma once

#include <QCoro/QCoroTask>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>

#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

namespace px::modrim {

class CoreContext;
class DiagnosticsService;

class SortService {
public:
    // Neither argument is owned; both must outlive the service.
    SortService(CoreContext& ctx, DiagnosticsService& diagnostics);

    // Orders the active mods by tier, dependencies and load-order rules. Resolves to their uuids
    // in that order.
    QCoro::Task<QStringList> sort_active_mods();

    // Uuids of inactive mods that the active mods need and that are not already covered.
    QStringList resolve_missing_dependencies(const QSet<QString>& active_uuids) const;

    // Maps topologically sorted package ids back to the uuids of the active mods that declare them.
    // Only the active uuid of a package id is considered, so an inactive duplicate never stands in
    // for an active mod.
    static QStringList order_uuids(const QList<sort::PackageId>& sorted_pids,
                                   const QHash<QString, ListedModPtr>& all_mods,
                                   const QStringList& active_uuids);

    // Uuids of inactive mods that satisfy a dependency of an active mod which no active mod (or
    // alternative package id) already covers. Duplicates are removed, keeping first occurrence.
    static QStringList missing_dependency_uuids(const QHash<QString, ListedModPtr>& all_mods,
                                                const QSet<QString>& active_uuids);

private:
    CoreContext& ctx_;
    DiagnosticsService& diagnostics_;
};

}  // namespace px::modrim
