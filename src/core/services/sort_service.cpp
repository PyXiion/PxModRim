#include "core/services/sort_service.hpp"

#include <QtConcurrent>
#include <algorithm>
#include <memory>

#include <QCoro/QCoroFuture>

#include "core/checker/sort.hpp"
#include "core/context.hpp"
#include "core/profiler.hpp"
#include "core/services/diagnostics_service.hpp"

namespace px::modrim {

namespace {

std::shared_ptr<AboutXmlMod> aboutXml(const ListedModPtr& mod) {
  return std::dynamic_pointer_cast<AboutXmlMod>(mod);
}

}  // namespace

SortService::SortService(CoreContext& ctx, DiagnosticsService& diagnostics)
    : m_ctx(ctx), m_diagnostics(diagnostics) {}

// Python: asyncio.to_thread(topological_sort, ...) -> QtConcurrent::run +
// co_await. The inputs are copies or borrowed from the diagnostics service,
// which is not mutated while awaiting.
QCoro::Task<QStringList> SortService::sortActiveMods() {
  ProfileSession session(QStringLiteral("sort"));
  Profiler& timer = session.timer();
  const auto activeMods = m_diagnostics.activeModsByPid();
  const auto& graph = m_diagnostics.constraintGraph();
  const sort::SortSettings settings = m_ctx.config().sort;
  const sort::CommunityRules* rules = &m_diagnostics.communityRules();

  const QList<sort::PackageId> sorted = co_await QtConcurrent::run([&] {
    return checker::topologicalSort(activeMods, graph, settings, rules, &timer);
  });
  co_return orderUuids(sorted, m_ctx.allMods(), m_ctx.activeUuids());
}

QStringList SortService::resolveMissingDependencies(
    const QSet<QString>& activeUuids) const {
  return missingDependencyUuids(m_ctx.allMods(), activeUuids);
}

QStringList SortService::orderUuids(const QList<sort::PackageId>& sortedPids,
                                    const QHash<QString, ListedModPtr>& allMods,
                                    const QStringList& activeUuids) {
  QHash<sort::PackageId, QString> pidToUuid;
  for (const auto& uuid : activeUuids) {
    if (auto about = aboutXml(allMods.value(uuid))) {
      pidToUuid.insert(about->packageId, uuid);
    }
  }

  QStringList ordered;
  for (const auto& pid : sortedPids) {
    if (auto it = pidToUuid.constFind(pid); it != pidToUuid.constEnd()) {
      ordered.append(it.value());
    }
  }
  return ordered;
}

QStringList SortService::missingDependencyUuids(
    const QHash<QString, ListedModPtr>& allMods,
    const QSet<QString>& activeUuids) {
  // Visiting uuids in sorted order keeps the choice between duplicate inactive
  // copies stable.
  QStringList allUuids = allMods.keys();
  std::ranges::sort(allUuids);
  QHash<QString, QString> pidToUuid;
  for (const auto& uuid : allUuids) {
    if (auto about = aboutXml(allMods.value(uuid))) {
      pidToUuid.insert(about->packageId.str(), uuid);
    }
  }

  QSet<QString> activePids;
  for (const auto& uuid : activeUuids) {
    if (auto about = aboutXml(allMods.value(uuid))) {
      activePids.insert(about->packageId.str());
    }
  }

  QStringList missing;
  for (const auto& uuid : activeUuids) {
    auto about = aboutXml(allMods.value(uuid));
    if (!about) {
      continue;
    }
    for (const auto& dep : about->aboutRules.dependencies) {
      const QString depPid = dep.packageId.str();
      if (activePids.contains(depPid)) {
        continue;
      }
      const bool satisfiedByAlternative = std::ranges::any_of(
          dep.alternativePackageIds, [&](const CaseInsensitiveStr& alt) {
            return activePids.contains(alt.str());
          });
      if (satisfiedByAlternative) {
        continue;
      }
      auto depUuid = pidToUuid.constFind(depPid);
      if (depUuid != pidToUuid.constEnd() &&
          !activeUuids.contains(depUuid.value())) {
        missing.append(depUuid.value());
      }
    }
  }

  missing.removeDuplicates();
  return missing;
}

}  // namespace px::modrim
