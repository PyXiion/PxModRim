#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <cstdint>
#include <memory>

#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

namespace px::modrim::checker {

using PackageId = CaseInsensitiveStr;
using AboutXmlModPtr = std::shared_ptr<AboutXmlMod>;

class ConstraintGraph;

enum class IssueSeverity : std::uint8_t { kError, kWarning };

struct ModIssue {
  QString category;
  IssueSeverity severity;
  QString shortMessage;
  QString categoryDisplayName;
  QString detailMessage;
  QList<PackageId> relatedPackageIds;

  bool operator==(const ModIssue&) const = default;
};

struct ModDiagnostics {
  QList<ModIssue> errors;
  QList<ModIssue> warnings;

  bool hasErrors() const noexcept { return !errors.isEmpty(); }
  bool hasWarnings() const noexcept { return !warnings.isEmpty(); }
  // The single issue's message, or a count once there is more than one.
  QString errorTooltip() const;
  QString warningTooltip() const;

  bool operator==(const ModDiagnostics&) const = default;
};

// Everything a checker may read while checking one mod. Callers own the
// referenced data and keep it alive for the duration of the check.
struct CheckContext {
  const QHash<PackageId, AboutXmlModPtr>& activeMods;
  const QList<PackageId>& orderedPids;
  const QHash<PackageId, int>& pidToIndex;
  const ConstraintGraph& graph;
  const sort::SortSettings& settings;
  QString targetVersion = QStringLiteral("1.5");
  const QSet<PackageId>& noVersionWarning;
  const QHash<QString, ReplacementInfo>& useThisInstead;
  const QList<QList<PackageId>>& cycles;
};

}  // namespace px::modrim::checker
