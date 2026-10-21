#include "core/services/sort_service.hpp"

#include <QtConcurrent>
#include <algorithm>
#include <memory>

#include <QCoro/QCoroFuture>

#include <spdlog/spdlog.h>

#include "core/checker/sort.hpp"
#include "core/context.hpp"
#include "core/profiler.hpp"
#include "core/services/diagnostics_service.hpp"

namespace px::modrim {

namespace {

std::shared_ptr<AboutXmlMod> aboutXml(const ListedModPtr& mod) {
  return std::dynamic_pointer_cast<AboutXmlMod>(mod);
}

// Everything the sort worker reads, so it never touches live service state.
struct SortInputs {
  QHash<sort::PackageId, checker::AboutXmlModPtr> activeMods;
  checker::ConstraintGraph graph;
  sort::CommunityRules rules;
  sort::SortSettings settings;
  QHash<QString, ListedModPtr> allMods;
  QStringList activeUuids;
};

}  // namespace

SortService::SortService(CoreContext& ctx, DiagnosticsService& diagnostics)
    : m_ctx(ctx), m_diagnostics(diagnostics) {}

QCoro::Task<std::optional<QStringList>> SortService::sortActiveMods() {
  // Read before the snapshot: any later mutation bumps the revision and
  // invalidates whatever the worker produces.
  const quint64 revision = m_ctx.sortInputsRevision();
  const SortInputs inputs{
      .activeMods = m_diagnostics.activeModsByPid(),
      .graph = m_diagnostics.constraintGraph(),
      .rules = m_diagnostics.communityRules(),
      .settings = m_ctx.config().sort,
      .allMods = m_ctx.allMods(),
      .activeUuids = m_ctx.activeUuids(),
  };

  const QList<sort::PackageId> sorted = co_await QtConcurrent::run([inputs] {
    ProfileSession session(QStringLiteral("sort"));
    return checker::topologicalSort(inputs.activeMods, inputs.graph,
                                    inputs.settings, &inputs.rules,
                                    &session.timer());
  });
  if (m_ctx.sortInputsRevision() != revision) {
    spdlog::warn("Sort discarded: the mod list changed while sorting");
    co_return std::nullopt;
  }
  co_return orderUuids(sorted, inputs.allMods, inputs.activeUuids);
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
