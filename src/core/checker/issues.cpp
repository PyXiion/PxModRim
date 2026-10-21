#include "core/checker/issues.hpp"

#include <algorithm>
#include <utility>

#include "core/checker/graph.hpp"

namespace px::modrim::checker {

bool DependencyIssueChecker::shouldCheck(const AboutXmlMod& mod,
                                         const CheckContext&) const {
  return !mod.aboutRules.dependencies.isEmpty();
}

QList<ModIssue> DependencyIssueChecker::check(const AboutXmlMod& mod,
                                              const CheckContext& ctx) const {
  if (!ctx.settings.check_missing_dependencies) {
    return {};
  }

  QList<ModIssue> issues;
  const bool considerAlternatives = ctx.settings.use_alternative_package_ids;

  for (const DependencyMod& dep : mod.aboutRules.dependencies) {
    bool satisfied = ctx.activeMods.contains(dep.packageId);
    if (!satisfied && considerAlternatives) {
      satisfied = std::ranges::any_of(
          dep.alternativePackageIds,
          [&](const PackageId& alt) { return ctx.activeMods.contains(alt); });
    }
    if (satisfied) {
      continue;
    }

    QString altNote;
    if (considerAlternatives && !dep.alternativePackageIds.empty()) {
      QStringList candidates;
      for (const PackageId& alt : dep.alternativePackageIds) {
        candidates.append(alt.str());
      }
      altNote = QStringLiteral(" (alternatives: %1)")
                    .arg(candidates.join(QStringLiteral(", ")));
    }

    issues.append(ModIssue{
        .severity = IssueSeverity::kError,
        .shortMessage = QStringLiteral("Missing dependencies"),
        .categoryDisplayName = categoryDisplayName(),
        .detailMessage = QStringLiteral("Dependency not satisfied: %1%2")
                             .arg(dep.packageId.str(), altNote),
    });
  }

  return issues;
}

bool IncompatibilityIssueChecker::shouldCheck(const AboutXmlMod& mod,
                                              const CheckContext& ctx) const {
  const PackageId& pid = mod.packageId;
  return !mod.aboutRules.incompatibleWith.empty() ||
         !ctx.graph.edgesOfType(pid, EdgeType::kIncompatibility).isEmpty() ||
         !ctx.graph.incomingOfType(pid, EdgeType::kIncompatibility).isEmpty();
}

QList<ModIssue> IncompatibilityIssueChecker::check(
    const AboutXmlMod& mod, const CheckContext& ctx) const {
  QList<ModIssue> issues;
  const PackageId& pid = mod.packageId;
  const auto incompatibility = [this](QString detail) {
    return ModIssue{
        .severity = IssueSeverity::kError,
        .shortMessage = QStringLiteral("Incompatibilities detected"),
        .categoryDisplayName = categoryDisplayName(),
        .detailMessage = std::move(detail),
    };
  };

  for (const PackageId& incomp : mod.aboutRules.incompatibleWith) {
    if (!ctx.activeMods.contains(incomp)) {
      continue;
    }
    const AboutXmlModPtr other = ctx.activeMods.value(incomp);
    issues.append(incompatibility(
        QStringLiteral("Mod declares incompatibility with: %1 (%2)")
            .arg(other->name, incomp.str())));
  }

  // Each declaring mod is reported once, even when it is seen both in our own
  // list and as an incoming graph edge (about.xml and community rules can both
  // declare the same pair).
  QSet<PackageId> reportedSources(mod.aboutRules.incompatibleWith.begin(),
                                  mod.aboutRules.incompatibleWith.end());
  for (const ConstraintEdge& edge :
       ctx.graph.incomingOfType(pid, EdgeType::kIncompatibility)) {
    if (!ctx.activeMods.contains(edge.source) || edge.source == pid ||
        reportedSources.contains(edge.source)) {
      continue;
    }
    reportedSources.insert(edge.source);
    const AboutXmlModPtr other = ctx.activeMods.value(edge.source);
    issues.append(
        incompatibility(QStringLiteral("Declared incompatible by: %1 (%2)")
                            .arg(other->name, edge.source.str())));
  }

  return issues;
}

bool LoadOrderIssueChecker::shouldCheck(const AboutXmlMod& mod,
                                        const CheckContext& ctx) const {
  const PackageId& pid = mod.packageId;
  return !ctx.graph.edgesOfType(pid, EdgeType::kLoadBefore).isEmpty() ||
         !ctx.graph.edgesOfType(pid, EdgeType::kLoadAfter).isEmpty();
}

std::optional<QSet<PackageId>> LoadOrderIssueChecker::affectedByMoves(
    const CheckContext& ctx, const QSet<PackageId>& moved) const {
  // Results only read the indices of the mod itself and its edge targets.
  QSet<PackageId> affected = moved;
  for (const PackageId& pid : moved) {
    for (const EdgeType type : {EdgeType::kLoadBefore, EdgeType::kLoadAfter}) {
      for (const ConstraintEdge& edge : ctx.graph.incomingOfType(pid, type)) {
        affected.insert(edge.source);
      }
    }
  }
  return affected;
}

QList<ModIssue> LoadOrderIssueChecker::check(const AboutXmlMod& mod,
                                             const CheckContext& ctx) const {
  QList<ModIssue> issues;
  const PackageId& pid = mod.packageId;
  const int idx = ctx.pidToIndex.value(pid, -1);
  if (idx < 0) {
    return issues;
  }

  struct Rule {
    EdgeType type;
    bool (*violated)(int idx, int otherIdx);
    const char* message;
  };
  const Rule rules[] = {
      {EdgeType::kLoadBefore,
       [](int idx, int otherIdx) { return idx >= otherIdx; },
       "Should load before: %1 (%2), but appears after (index %3 vs %4)"},
      {EdgeType::kLoadAfter,
       [](int idx, int otherIdx) { return idx <= otherIdx; },
       "Should load after: %1 (%2), but appears before (index %3 vs %4)"},
  };
  for (const Rule& rule : rules) {
    for (const ConstraintEdge& edge : ctx.graph.edgesOfType(pid, rule.type)) {
      const int otherIdx = ctx.pidToIndex.value(edge.target, -1);
      if (otherIdx < 0 || !rule.violated(idx, otherIdx)) {
        continue;
      }
      const AboutXmlModPtr other = ctx.activeMods.value(edge.target);
      const QString name = other ? other->name : edge.target.str();
      issues.append(ModIssue{
          .severity = IssueSeverity::kWarning,
          .shortMessage = QStringLiteral("Load order violated"),
          .categoryDisplayName = categoryDisplayName(),
          .detailMessage = QString::fromLatin1(rule.message)
                               .arg(name, edge.target.str())
                               .arg(idx)
                               .arg(otherIdx),
      });
    }
  }

  return issues;
}

QList<ModIssue> CycleIssueChecker::check(const AboutXmlMod& mod,
                                         const CheckContext& ctx) const {
  const PackageId& pid = mod.packageId;
  QList<ModIssue> issues;

  for (const QList<PackageId>& cycle : ctx.cycles) {
    if (!cycle.contains(pid)) {
      continue;
    }
    QStringList members;
    for (const PackageId& member : cycle) {
      members.append(member.str());
    }
    issues.append(ModIssue{
        .severity = IssueSeverity::kWarning,
        .shortMessage = QStringLiteral("Dependency cycle detected"),
        .categoryDisplayName = categoryDisplayName(),
        .detailMessage =
            QStringLiteral("Participates in a dependency cycle involving: ") +
            members.join(QStringLiteral(", ")),
    });
  }

  return issues;
}

bool GameVersionIssueChecker::shouldCheck(const AboutXmlMod& mod,
                                          const CheckContext&) const {
  return !mod.supportedVersions.isEmpty();
}

QList<ModIssue> GameVersionIssueChecker::check(const AboutXmlMod& mod,
                                               const CheckContext& ctx) const {
  if (mod.supportedVersions.isEmpty()) {
    return {};
  }
  if (ctx.noVersionWarning.contains(mod.packageId)) {
    return {};
  }

  // Only major.minor is compared, so a patch-level target still matches "1.5"
  // support.
  const QStringList targetParts =
      ctx.targetVersion.split(QLatin1Char('.')).mid(0, 2);
  const QString targetMajorMinor = targetParts.join(QLatin1Char('.'));
  if (mod.supportedVersions.contains(targetMajorMinor)) {
    return {};
  }

  QStringList supported(mod.supportedVersions.cbegin(),
                        mod.supportedVersions.cend());
  std::ranges::sort(supported);
  return {ModIssue{
      .severity = IssueSeverity::kWarning,
      .shortMessage = QStringLiteral("Version mismatch"),
      .categoryDisplayName = categoryDisplayName(),
      .detailMessage =
          QStringLiteral("Mod supports %1, but game is on %2")
              .arg(supported.join(QStringLiteral(", ")), targetMajorMinor),
  }};
}

QList<ModIssue> ReplacementIssueChecker::check(const AboutXmlMod& mod,
                                               const CheckContext& ctx) const {
  const std::optional<QString> pfid = mod.publishedFileId();
  if (!pfid) {
    return {};
  }

  const auto replacement = ctx.useThisInstead.constFind(*pfid);
  if (replacement == ctx.useThisInstead.cend()) {
    return {};
  }
  const ReplacementInfo& info = replacement.value();

  return {ModIssue{
      .severity = IssueSeverity::kWarning,
      .shortMessage = QStringLiteral("Replacement available"),
      .categoryDisplayName = categoryDisplayName(),
      .detailMessage = QStringLiteral("Recommended replacement: %1 (%2) by %3")
                           .arg(info.name, info.packageid, info.author),
  }};
}

}  // namespace px::modrim::checker
