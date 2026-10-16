#pragma once

#include <cstdint>
#include <memory>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "core/models/metadata/structures.h"
#include "core/sort/config.h"

namespace px::modrim::checker {

using PackageId = CaseInsensitiveStr;
using AboutXmlModPtr = std::shared_ptr<AboutXmlMod>;

class ConstraintGraph;

enum class IssueSeverity : std::uint8_t { Error, Warning };

struct ModIssue {
    QString category;
    IssueSeverity severity;
    QString short_message;
    QString category_display_name;
    QString detail_message;
    QList<PackageId> related_package_ids;

    bool operator==(const ModIssue&) const = default;
};

struct ModDiagnostics {
    QList<ModIssue> errors;
    QList<ModIssue> warnings;

    bool has_errors() const noexcept { return !errors.isEmpty(); }
    bool has_warnings() const noexcept { return !warnings.isEmpty(); }
    // The single issue's message, or a count once there is more than one.
    QString error_tooltip() const;
    QString warning_tooltip() const;

    bool operator==(const ModDiagnostics&) const = default;
};

// Everything a checker may read while checking one mod. Callers own the referenced data and
// keep it alive for the duration of the check.
struct CheckContext {
    const QHash<PackageId, AboutXmlModPtr>& active_mods;
    const QList<PackageId>& ordered_pids;
    const QHash<PackageId, int>& pid_to_index;
    const ConstraintGraph& graph;
    const sort::SortSettings& settings;
    QString target_version = QStringLiteral("1.5");
    const QSet<PackageId>& no_version_warning;
    const QHash<QString, ReplacementInfo>& use_this_instead;
    const QList<QList<PackageId>>& cycles;
};

}  // namespace px::modrim::checker
