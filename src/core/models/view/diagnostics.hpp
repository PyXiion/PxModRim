#pragma once

#include <QString>
#include <optional>

namespace px::modrim::view {

// UI-agnostic diagnostics summary for one mod.
struct ModDiagnosticsView {
  bool hasErrors = false;
  bool hasWarnings = false;
  QString errorTooltip;
  QString warningTooltip;

  bool operator==(const ModDiagnosticsView&) const = default;
};

struct ModIssueView {
  QString categoryDisplayName;
  std::optional<QString> detail;
  bool isError = false;

  bool operator==(const ModIssueView&) const = default;
};

}  // namespace px::modrim::view
