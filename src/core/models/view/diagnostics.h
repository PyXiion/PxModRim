#pragma once

#include <optional>

#include <QString>

namespace px::core::view {

// UI-agnostic diagnostics summary for one mod.
struct ModDiagnosticsView {
    bool has_errors = false;
    bool has_warnings = false;
    QString error_tooltip;
    QString warning_tooltip;

    bool operator==(const ModDiagnosticsView&) const = default;
};

struct ModIssueView {
    QString category;
    QString category_display_name;
    std::optional<QString> detail;
    bool is_error = false;

    bool operator==(const ModIssueView&) const = default;
};

}  // namespace px::core::view
