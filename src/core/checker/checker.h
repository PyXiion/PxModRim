#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/checker/graph.h"
#include "core/checker/issues.h"
#include "core/checker/models.h"
#include "core/models/metadata/structures.h"
#include "core/profiler.h"
#include "core/sort/config.h"
#include "core/sort/models.h"

namespace px::modrim::checker {

// Diagnostics with at least one issue, keyed by mod uuid.
using DiagnosticsByUuid = QHash<QString, ModDiagnostics>;
using DiagnosticsCallback = std::function<void(const DiagnosticsByUuid&)>;

// Runs every checker over the active mods and keeps their diagnostics up to date as the mod
// list, load order and databases change.
class ModChecker {
public:
    ModChecker(std::vector<std::unique_ptr<ModIssueChecker>> checkers,
               sort::SortSettings settings,
               QString target_version = QStringLiteral("1.5"),
               DiagnosticsCallback on_diagnostics_changed = {});

    // ── Database config ───────────────────────────────────────

    void set_no_version_warning(QSet<PackageId> pids);
    void set_use_this_instead(QHash<QString, ReplacementInfo> db);
    void set_community_rules(sort::CommunityRules rules);

    // ── Rebuild ───────────────────────────────────────────────

    // Re-scans all mods and regenerates diagnostics from scratch.
    void rebuild(const QHash<QString, ListedModPtr>& mods,
                 const QStringList& ordered_uuids,
                 Profiler* timer = nullptr);

    // ── Incremental updates ───────────────────────────────────

    // Rebuilds constraints and diagnostics after toggling `mod`. Non-About.xml mods only emit.
    void toggle_mod(const ListedMod& mod, const QStringList& ordered_uuids);

    // Moves a mod to a new position and rechecks it and its graph neighbours.
    void move_mod(const QString& uuid, int old_index, int new_index);

    // Reapplies the full active-mod order and regenerates the affected diagnostics.
    void reorder(const QStringList& ordered_uuids);

    // ── Query ─────────────────────────────────────────────────

    QHash<PackageId, AboutXmlModPtr> active_mods() const { return active_mods_; }
    const ConstraintGraph& graph() const noexcept { return graph_; }
    QList<PackageId> ordered_pids() const { return ordered_pids_; }
    const sort::CommunityRules& community_rules() const noexcept { return community_rules_; }

    // Null when `uuid` is not an active mod. The pointer is valid until the next mutation.
    const ModDiagnostics* diagnostics_for(const QString& uuid) const;
    DiagnosticsByUuid active_mod_diagnostics() const;

private:
    using CheckerResults = QHash<PackageId, std::vector<QList<ModIssue>>>;

    void collect_active(const QHash<QString, ListedModPtr>& mods, const QStringList& ordered_uuids);
    CheckContext build_context(const QList<QList<PackageId>>& cycles) const;
    void check_all(const CheckContext& ctx);
    ModDiagnostics check_mod(const AboutXmlMod& mod, const CheckContext& ctx) const;
    QList<ModIssue> run_checker(const ModIssueChecker& checker,
                                const AboutXmlMod& mod,
                                const CheckContext& ctx) const;
    void emit_diagnostics() const;

    std::vector<std::unique_ptr<ModIssueChecker>> checkers_;
    sort::SortSettings settings_;
    QString target_version_;
    DiagnosticsCallback on_diagnostics_changed_;

    ConstraintGraph graph_;
    QHash<QString, ListedModPtr> all_mods_;
    QHash<PackageId, ModDiagnostics> diagnostics_;
    QHash<PackageId, AboutXmlModPtr> active_mods_;
    QHash<QString, PackageId> uuid_to_pid_;
    QList<PackageId> ordered_pids_;

    QSet<PackageId> no_version_warning_;
    QHash<QString, ReplacementInfo> use_this_instead_;
    sort::CommunityRules community_rules_;
    QList<QList<PackageId>> cached_cycles_;
    // Per-mod issues from each checker (checker order), reused by reorder() for
    // order-independent checkers; nullopt when stale.
    std::optional<CheckerResults> checker_results_;
};

}  // namespace px::modrim::checker
