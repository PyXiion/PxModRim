#include "core/checker/checker.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

namespace px::modrim::checker {
namespace {

ModDiagnostics compose(const std::vector<QList<ModIssue>>& perChecker) {
  ModDiagnostics diagnostics;
  for (const auto& issues : perChecker) {
    for (const ModIssue& issue : issues) {
      if (issue.severity == IssueSeverity::kError) {
        diagnostics.errors.append(issue);
      } else {
        diagnostics.warnings.append(issue);
      }
    }
  }
  return diagnostics;
}

// Same key set, and every entry is the same mod object (identity, not
// equality).
bool sameMods(const QHash<PackageId, AboutXmlModPtr>& a,
              const QHash<PackageId, AboutXmlModPtr>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (auto it = a.cbegin(); it != a.cend(); ++it) {
    const auto other = b.constFind(it.key());
    if (other == b.cend() || other.value() != it.value()) {
      return false;
    }
  }
  return true;
}

}  // namespace

ModChecker::ModChecker(std::vector<std::unique_ptr<ModIssueChecker>> checkers,
                       sort::SortSettings settings, QString targetVersion,
                       DiagnosticsCallback onDiagnosticsChanged)
    : m_checkers(std::move(checkers)),
      m_settings(std::move(settings)),
      m_targetVersion(std::move(targetVersion)),
      m_onDiagnosticsChanged(std::move(onDiagnosticsChanged)) {}

void ModChecker::setNoVersionWarning(QSet<PackageId> pids) {
  m_noVersionWarning = std::move(pids);
  m_checkerResults.reset();
}

void ModChecker::setUseThisInstead(QHash<QString, ReplacementInfo> db) {
  m_useThisInstead = std::move(db);
  m_checkerResults.reset();
}

void ModChecker::setCommunityRules(sort::CommunityRules rules) {
  m_communityRules = std::move(rules);
}

void ModChecker::rebuild(const QHash<QString, ListedModPtr>& mods,
                         const QStringList& orderedUuids, Profiler* timer) {
  Profiler local;
  Profiler& t = timer != nullptr ? *timer : local;

  {
    auto scope = t("collect_active");
    collectActive(mods, orderedUuids);
  }
  {
    auto scope = t("graph.build");
    m_graph.build(m_activeMods, m_orderedPids, m_settings, &m_communityRules);
  }
  {
    auto scope = t("find_cycles");
    m_cachedCycles = m_graph.findCycles();
  }
  const CheckContext ctx = buildContext(m_cachedCycles);
  {
    auto scope = t("check_mod");
    checkAll(ctx);
  }

  emitDiagnostics();
}

void ModChecker::reorder(const QStringList& orderedUuids) {
  const QHash<PackageId, AboutXmlModPtr> previous = m_activeMods;
  const QHash<PackageId, int> previousIndex = m_graph.pidToIndex();
  collectActive(m_allMods, orderedUuids);
  m_graph.updateOrder(m_orderedPids);

  const QList<QList<PackageId>> cycles = m_graph.findCycles();
  const CheckContext ctx = buildContext(cycles);

  if (!m_checkerResults.has_value() || !sameMods(previous, m_activeMods)) {
    checkAll(ctx);
  } else {
    QSet<PackageId> moved;
    for (auto it = ctx.pidToIndex.cbegin(); it != ctx.pidToIndex.cend(); ++it) {
      if (previousIndex.value(it.key(), -1) != it.value()) {
        moved.insert(it.key());
      }
    }

    CheckerResults& results = *m_checkerResults;
    QSet<PackageId> changed;
    for (std::size_t i = 0; i < m_checkers.size(); ++i) {
      const ModIssueChecker& checker = *m_checkers[i];
      if (!checker.orderDependent()) {
        continue;
      }
      const auto affected = checker.affectedByMoves(ctx, moved);
      for (auto it = m_activeMods.cbegin(); it != m_activeMods.cend(); ++it) {
        if (affected && !affected->contains(it.key())) {
          continue;
        }
        QList<ModIssue> issues = runChecker(checker, *it.value(), ctx);
        auto& stored = results[it.key()][i];
        if (issues != stored) {
          stored = std::move(issues);
          changed.insert(it.key());
        }
      }
    }
    for (const PackageId& pid : changed) {
      m_diagnostics.insert(pid, compose(results[pid]));
    }
  }

  emitDiagnostics();
}

const ModDiagnostics* ModChecker::diagnosticsFor(const QString& uuid) const {
  const auto pid = m_uuidToPid.constFind(uuid);
  if (pid == m_uuidToPid.cend()) {
    return nullptr;
  }
  const auto diagnostics = m_diagnostics.constFind(pid.value());
  return diagnostics == m_diagnostics.cend() ? nullptr : &diagnostics.value();
}

DiagnosticsByUuid ModChecker::activeModDiagnostics() const {
  DiagnosticsByUuid result;
  for (auto it = m_uuidToPid.cbegin(); it != m_uuidToPid.cend(); ++it) {
    if (const auto diagnostics = m_diagnostics.constFind(it.value());
        diagnostics != m_diagnostics.cend()) {
      result.insert(it.key(), diagnostics.value());
    }
  }
  return result;
}

void ModChecker::collectActive(const QHash<QString, ListedModPtr>& mods,
                               const QStringList& orderedUuids) {
  // `mods` may alias all_mods_ (toggle_mod and reorder pass it back in), so
  // copy first.
  m_allMods = mods;
  m_activeMods.clear();
  m_uuidToPid.clear();
  m_orderedPids.clear();

  for (const QString& uuid : orderedUuids) {
    const auto listed = m_allMods.constFind(uuid);
    if (listed == m_allMods.cend()) {
      continue;
    }
    auto about = std::dynamic_pointer_cast<AboutXmlMod>(listed.value());
    if (!about) {
      continue;
    }
    const PackageId pid = about->packageId;
    m_activeMods.insert(pid, about);
    m_uuidToPid.insert(uuid, pid);
    m_orderedPids.append(pid);
  }
}

CheckContext ModChecker::buildContext(
    const QList<QList<PackageId>>& cycles) const {
  return CheckContext{
      .activeMods = m_activeMods,
      .orderedPids = m_orderedPids,
      .pidToIndex = m_graph.pidToIndex(),
      .graph = m_graph,
      .settings = m_settings,
      .targetVersion = m_targetVersion,
      .noVersionWarning = m_noVersionWarning,
      .useThisInstead = m_useThisInstead,
      .cycles = cycles,
  };
}

void ModChecker::checkAll(const CheckContext& ctx) {
  CheckerResults results;
  QHash<PackageId, ModDiagnostics> diagnostics;
  for (auto it = m_activeMods.cbegin(); it != m_activeMods.cend(); ++it) {
    std::vector<QList<ModIssue>> perChecker;
    perChecker.reserve(m_checkers.size());
    for (const auto& checker : m_checkers) {
      perChecker.push_back(runChecker(*checker, *it.value(), ctx));
    }
    diagnostics.insert(it.key(), compose(perChecker));
    results.insert(it.key(), std::move(perChecker));
  }
  m_checkerResults = std::move(results);
  m_diagnostics = std::move(diagnostics);
}

QList<ModIssue> ModChecker::runChecker(const ModIssueChecker& checker,
                                       const AboutXmlMod& mod,
                                       const CheckContext& ctx) const {
  if (!checker.shouldCheck(mod, ctx)) {
    return {};
  }
  // One checker must not abort diagnostics for every other mod and checker.
  try {
    return checker.check(mod, ctx);
  } catch (const std::exception& error) {
    spdlog::error("Checker failed on {}: {}", mod.packageId.str().toStdString(),
                  error.what());
    return {};
  }
}

void ModChecker::emitDiagnostics() const {
  if (!m_onDiagnosticsChanged) {
    return;
  }
  DiagnosticsByUuid byUuid = activeModDiagnostics();
  for (auto it = byUuid.begin(); it != byUuid.end();) {
    if (it->hasErrors() || it->hasWarnings()) {
      ++it;
    } else {
      it = byUuid.erase(it);
    }
  }
  m_onDiagnosticsChanged(byUuid);
}

}  // namespace px::modrim::checker
