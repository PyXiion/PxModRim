#include "core/checker/issues.h"

#include <algorithm>

#include "core/checker/graph.h"

namespace px::modrim::checker {

bool DependencyIssueChecker::should_check(const AboutXmlMod& mod, const CheckContext&) const {
    return !mod.about_rules.dependencies.isEmpty();
}

QList<ModIssue> DependencyIssueChecker::check(const AboutXmlMod& mod,
                                              const CheckContext& ctx) const {
    if (!ctx.settings.check_missing_dependencies) {
        return {};
    }

    QList<ModIssue> issues;
    const bool consider_alternatives = ctx.settings.use_alternative_package_ids;

    for (const DependencyMod& dep : mod.about_rules.dependencies) {
        bool satisfied = ctx.active_mods.contains(dep.package_id);
        if (!satisfied && consider_alternatives) {
            satisfied = std::ranges::any_of(dep.alternative_package_ids, [&](const PackageId& alt) {
                return ctx.active_mods.contains(alt);
            });
        }
        if (satisfied) {
            continue;
        }

        QString alt_note;
        if (consider_alternatives && !dep.alternative_package_ids.empty()) {
            QStringList candidates;
            for (const PackageId& alt : dep.alternative_package_ids) {
                candidates.append(alt.str());
            }
            alt_note = QStringLiteral(" (alternatives: %1)").arg(candidates.join(QStringLiteral(", ")));
        }

        issues.append(ModIssue{
            .category = QStringLiteral("missing_dependency"),
            .severity = IssueSeverity::Error,
            .short_message = QStringLiteral("Missing dependencies"),
            .category_display_name = category_display_name(),
            .detail_message =
                QStringLiteral("Dependency not satisfied: %1%2").arg(dep.package_id.str(), alt_note),
            .related_package_ids = {dep.package_id},
        });
    }

    return issues;
}

bool IncompatibilityIssueChecker::should_check(const AboutXmlMod& mod,
                                               const CheckContext& ctx) const {
    const PackageId& pid = mod.package_id;
    return !mod.about_rules.incompatible_with.empty() ||
           !ctx.graph.edges_of_type(pid, EdgeType::Incompatibility).isEmpty() ||
           !ctx.graph.incoming_of_type(pid, EdgeType::Incompatibility).isEmpty();
}

QList<ModIssue> IncompatibilityIssueChecker::check(const AboutXmlMod& mod,
                                                   const CheckContext& ctx) const {
    QList<ModIssue> issues;
    const PackageId& pid = mod.package_id;

    for (const PackageId& incomp : mod.about_rules.incompatible_with) {
        if (!ctx.active_mods.contains(incomp)) {
            continue;
        }
        const AboutXmlModPtr other = ctx.active_mods.value(incomp);
        issues.append(ModIssue{
            .category = QStringLiteral("incompatibility"),
            .severity = IssueSeverity::Error,
            .short_message = QStringLiteral("Incompatibilities detected"),
            .category_display_name = category_display_name(),
            .detail_message = QStringLiteral("Mod declares incompatibility with: %1 (%2)")
                                  .arg(other->name, incomp.str()),
            .related_package_ids = {incomp},
        });
    }

    // Each declaring mod is reported once, even when it is seen both in our own list and as an
    // incoming graph edge (about.xml and community rules can both declare the same pair).
    QSet<PackageId> reported_sources(mod.about_rules.incompatible_with.begin(),
                                     mod.about_rules.incompatible_with.end());
    for (const ConstraintEdge& edge : ctx.graph.incoming_of_type(pid, EdgeType::Incompatibility)) {
        if (!ctx.active_mods.contains(edge.source) || edge.source == pid ||
            reported_sources.contains(edge.source)) {
            continue;
        }
        reported_sources.insert(edge.source);
        const AboutXmlModPtr other = ctx.active_mods.value(edge.source);
        issues.append(ModIssue{
            .category = QStringLiteral("incompatibility"),
            .severity = IssueSeverity::Error,
            .short_message = QStringLiteral("Incompatibilities detected"),
            .category_display_name = category_display_name(),
            .detail_message = QStringLiteral("Declared incompatible by: %1 (%2)")
                                  .arg(other->name, edge.source.str()),
            .related_package_ids = {edge.source},
        });
    }

    return issues;
}

bool LoadOrderIssueChecker::should_check(const AboutXmlMod& mod, const CheckContext& ctx) const {
    const PackageId& pid = mod.package_id;
    return !ctx.graph.edges_of_type(pid, EdgeType::LoadBefore).isEmpty() ||
           !ctx.graph.edges_of_type(pid, EdgeType::LoadAfter).isEmpty();
}

std::optional<QSet<PackageId>> LoadOrderIssueChecker::affected_by_moves(
    const CheckContext& ctx, const QSet<PackageId>& moved) const {
    // Results only read the indices of the mod itself and its edge targets.
    QSet<PackageId> affected = moved;
    for (const PackageId& pid : moved) {
        for (const EdgeType type : {EdgeType::LoadBefore, EdgeType::LoadAfter}) {
            for (const ConstraintEdge& edge : ctx.graph.incoming_of_type(pid, type)) {
                affected.insert(edge.source);
            }
        }
    }
    return affected;
}

QList<ModIssue> LoadOrderIssueChecker::check(const AboutXmlMod& mod,
                                             const CheckContext& ctx) const {
    QList<ModIssue> issues;
    const PackageId& pid = mod.package_id;
    const int idx = ctx.pid_to_index.value(pid, -1);
    if (idx < 0) {
        return issues;
    }

    for (const ConstraintEdge& edge : ctx.graph.edges_of_type(pid, EdgeType::LoadBefore)) {
        const int other_idx = ctx.pid_to_index.value(edge.target, -1);
        if (other_idx >= 0 && idx >= other_idx) {
            const AboutXmlModPtr other = ctx.active_mods.value(edge.target);
            const QString name = other ? other->name : edge.target.str();
            issues.append(ModIssue{
                .category = QStringLiteral("load_order"),
                .severity = IssueSeverity::Warning,
                .short_message = QStringLiteral("Load order violated"),
                .category_display_name = category_display_name(),
                .detail_message = QStringLiteral("Should load before: %1 (%2), but appears after "
                                                 "(index %3 vs %4)")
                                      .arg(name, edge.target.str())
                                      .arg(idx)
                                      .arg(other_idx),
                .related_package_ids = {edge.target},
            });
        }
    }

    for (const ConstraintEdge& edge : ctx.graph.edges_of_type(pid, EdgeType::LoadAfter)) {
        const int other_idx = ctx.pid_to_index.value(edge.target, -1);
        if (other_idx >= 0 && idx <= other_idx) {
            const AboutXmlModPtr other = ctx.active_mods.value(edge.target);
            const QString name = other ? other->name : edge.target.str();
            issues.append(ModIssue{
                .category = QStringLiteral("load_order"),
                .severity = IssueSeverity::Warning,
                .short_message = QStringLiteral("Load order violated"),
                .category_display_name = category_display_name(),
                .detail_message = QStringLiteral("Should load after: %1 (%2), but appears before "
                                                 "(index %3 vs %4)")
                                      .arg(name, edge.target.str())
                                      .arg(idx)
                                      .arg(other_idx),
                .related_package_ids = {edge.target},
            });
        }
    }

    return issues;
}

QList<ModIssue> CycleIssueChecker::check(const AboutXmlMod& mod, const CheckContext& ctx) const {
    const PackageId& pid = mod.package_id;
    QList<ModIssue> issues;

    for (const QList<PackageId>& cycle : ctx.cycles) {
        if (!cycle.contains(pid)) {
            continue;
        }
        QStringList members;
        QList<PackageId> related;
        for (const PackageId& member : cycle) {
            members.append(member.str());
            if (member != pid) {
                related.append(member);
            }
        }
        issues.append(ModIssue{
            .category = QStringLiteral("cycle"),
            .severity = IssueSeverity::Warning,
            .short_message = QStringLiteral("Dependency cycle detected"),
            .category_display_name = category_display_name(),
            .detail_message = QStringLiteral("Participates in a dependency cycle involving: ") +
                              members.join(QStringLiteral(", ")),
            .related_package_ids = std::move(related),
        });
    }

    return issues;
}

bool GameVersionIssueChecker::should_check(const AboutXmlMod& mod, const CheckContext&) const {
    return !mod.supported_versions.isEmpty();
}

QList<ModIssue> GameVersionIssueChecker::check(const AboutXmlMod& mod,
                                               const CheckContext& ctx) const {
    if (mod.supported_versions.isEmpty()) {
        return {};
    }
    if (ctx.no_version_warning.contains(mod.package_id)) {
        return {};
    }

    // Only major.minor is compared, so a patch-level target still matches "1.5" support.
    const QStringList target_parts = ctx.target_version.split(QLatin1Char('.')).mid(0, 2);
    const QString target_major_minor = target_parts.join(QLatin1Char('.'));
    if (mod.supported_versions.contains(target_major_minor)) {
        return {};
    }

    QStringList supported(mod.supported_versions.cbegin(), mod.supported_versions.cend());
    std::ranges::sort(supported);
    return {ModIssue{
        .category = QStringLiteral("version_mismatch"),
        .severity = IssueSeverity::Warning,
        .short_message = QStringLiteral("Version mismatch"),
        .category_display_name = category_display_name(),
        .detail_message = QStringLiteral("Mod supports %1, but game is on %2")
                              .arg(supported.join(QStringLiteral(", ")), target_major_minor),
        .related_package_ids = {},
    }};
}

QList<ModIssue> ReplacementIssueChecker::check(const AboutXmlMod& mod,
                                               const CheckContext& ctx) const {
    const std::optional<QString> pfid = mod.published_file_id();
    if (!pfid || pfid->isEmpty()) {
        return {};
    }

    const auto replacement = ctx.use_this_instead.constFind(*pfid);
    if (replacement == ctx.use_this_instead.cend()) {
        return {};
    }
    const ReplacementInfo& info = replacement.value();

    return {ModIssue{
        .category = QStringLiteral("replacement_available"),
        .severity = IssueSeverity::Warning,
        .short_message = QStringLiteral("Replacement available"),
        .category_display_name = category_display_name(),
        .detail_message = QStringLiteral("Recommended replacement: %1 (%2) by %3")
                              .arg(info.name, info.packageid, info.author),
        .related_package_ids = {PackageId(info.packageid)},
    }};
}

}  // namespace px::modrim::checker
