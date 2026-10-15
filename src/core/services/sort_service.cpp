#include "core/services/sort_service.h"

#include <algorithm>
#include <memory>

#include <QCoro/QCoroFuture>
#include <QtConcurrent>

#include "core/checker/sort.h"
#include "core/context.h"
#include "core/profiler.h"
#include "core/services/diagnostics_service.h"

namespace px::core {

namespace {

std::shared_ptr<AboutXmlMod> about_xml(const ListedModPtr& mod) {
    return std::dynamic_pointer_cast<AboutXmlMod>(mod);
}

}  // namespace

SortService::SortService(CoreContext& ctx, DiagnosticsService& diagnostics)
    : ctx_(ctx), diagnostics_(diagnostics) {}

// Python: asyncio.to_thread(topological_sort, ...) -> QtConcurrent::run + co_await. The inputs are
// copies or borrowed from the diagnostics service, which is not mutated while awaiting.
QCoro::Task<QStringList> SortService::sort_active_mods() {
    ProfileSession session(QStringLiteral("sort"));
    Profiler& timer = session.timer();
    const auto active_mods = diagnostics_.active_mods_by_pid();
    const auto& graph = diagnostics_.constraint_graph();
    const sort::SortSettings settings = ctx_.config().sort;
    const sort::CommunityRules* rules = &diagnostics_.community_rules();

    const QList<sort::PackageId> sorted = co_await QtConcurrent::run([&] {
        return checker::topological_sort(active_mods, graph, settings, rules, &timer);
    });
    co_return order_uuids(sorted, ctx_.allMods(), ctx_.activeUuids());
}

QStringList SortService::resolve_missing_dependencies(const QSet<QString>& active_uuids) const {
    return missing_dependency_uuids(ctx_.allMods(), active_uuids);
}

QStringList SortService::order_uuids(const QList<sort::PackageId>& sorted_pids,
                                     const QHash<QString, ListedModPtr>& all_mods,
                                     const QStringList& active_uuids) {
    QHash<sort::PackageId, QString> pid_to_uuid;
    for (const auto& uuid : active_uuids) {
        if (auto about = about_xml(all_mods.value(uuid))) {
            pid_to_uuid.insert(about->package_id, uuid);
        }
    }

    QStringList ordered;
    for (const auto& pid : sorted_pids) {
        if (auto it = pid_to_uuid.constFind(pid); it != pid_to_uuid.constEnd()) {
            ordered.append(it.value());
        }
    }
    return ordered;
}

QStringList SortService::missing_dependency_uuids(const QHash<QString, ListedModPtr>& all_mods,
                                                  const QSet<QString>& active_uuids) {
    // Visiting uuids in sorted order keeps the choice between duplicate inactive copies stable.
    QStringList all_uuids = all_mods.keys();
    std::ranges::sort(all_uuids);
    QHash<QString, QString> pid_to_uuid;
    for (const auto& uuid : all_uuids) {
        if (auto about = about_xml(all_mods.value(uuid))) {
            pid_to_uuid.insert(about->package_id.str(), uuid);
        }
    }

    QSet<QString> active_pids;
    for (const auto& uuid : active_uuids) {
        if (auto about = about_xml(all_mods.value(uuid))) {
            active_pids.insert(about->package_id.str());
        }
    }

    QStringList missing;
    for (const auto& uuid : active_uuids) {
        auto about = about_xml(all_mods.value(uuid));
        if (!about) {
            continue;
        }
        for (const auto& dep : about->about_rules.dependencies) {
            const QString dep_pid = dep.package_id.str();
            if (active_pids.contains(dep_pid)) {
                continue;
            }
            const bool satisfied_by_alternative = std::ranges::any_of(
                dep.alternative_package_ids, [&](const CaseInsensitiveStr& alt) {
                    return active_pids.contains(alt.str());
                });
            if (satisfied_by_alternative) {
                continue;
            }
            auto dep_uuid = pid_to_uuid.constFind(dep_pid);
            if (dep_uuid != pid_to_uuid.constEnd() && !active_uuids.contains(dep_uuid.value())) {
                missing.append(dep_uuid.value());
            }
        }
    }

    QStringList unique;
    for (const auto& uuid : missing) {
        if (!unique.contains(uuid)) {
            unique.append(uuid);
        }
    }
    return unique;
}

}  // namespace px::core
