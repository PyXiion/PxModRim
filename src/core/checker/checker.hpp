#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/checker/graph.hpp"
#include "core/checker/issues.hpp"
#include "core/checker/models.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/profiler.hpp"
#include "core/sort/config.hpp"
#include "core/sort/models.hpp"

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
               QString targetVersion = QStringLiteral("1.5"),
               DiagnosticsCallback onDiagnosticsChanged = {});

    // ── Database config ───────────────────────────────────────

    void setNoVersionWarning(QSet<PackageId> pids);
    void setUseThisInstead(QHash<QString, ReplacementInfo> db);
    void setCommunityRules(sort::CommunityRules rules);

    // ── Rebuild ───────────────────────────────────────────────

    // Re-scans all mods and regenerates diagnostics from scratch.
    void rebuild(const QHash<QString, ListedModPtr>& mods,
                 const QStringList& orderedUuids,
                 Profiler* timer = nullptr);

    // ── Incremental updates ───────────────────────────────────

    // Rebuilds constraints and diagnostics after toggling `mod`. Non-About.xml mods only emit.
    void toggleMod(const ListedMod& mod, const QStringList& orderedUuids);

    // Moves a mod to a new position and rechecks it and its graph neighbours.
    void moveMod(const QString& uuid, int oldIndex, int newIndex);

    // Reapplies the full active-mod order and regenerates the affected diagnostics.
    void reorder(const QStringList& orderedUuids);

    // ── Query ─────────────────────────────────────────────────

    QHash<PackageId, AboutXmlModPtr> activeMods() const { return m_activeMods; }
    const ConstraintGraph& graph() const noexcept { return m_graph; }
    QList<PackageId> orderedPids() const { return m_orderedPids; }
    const sort::CommunityRules& communityRules() const noexcept { return m_communityRules; }

    // Null when `uuid` is not an active mod. The pointer is valid until the next mutation.
    const ModDiagnostics* diagnosticsFor(const QString& uuid) const;
    DiagnosticsByUuid activeModDiagnostics() const;

private:
    using CheckerResults = QHash<PackageId, std::vector<QList<ModIssue>>>;

    void collectActive(const QHash<QString, ListedModPtr>& mods, const QStringList& orderedUuids);
    CheckContext buildContext(const QList<QList<PackageId>>& cycles) const;
    void checkAll(const CheckContext& ctx);
    ModDiagnostics checkMod(const AboutXmlMod& mod, const CheckContext& ctx) const;
    QList<ModIssue> runChecker(const ModIssueChecker& checker,
                                const AboutXmlMod& mod,
                                const CheckContext& ctx) const;
    void emitDiagnostics() const;

    std::vector<std::unique_ptr<ModIssueChecker>> m_checkers;
    sort::SortSettings m_settings;
    QString m_targetVersion;
    DiagnosticsCallback m_onDiagnosticsChanged;

    ConstraintGraph m_graph;
    QHash<QString, ListedModPtr> m_allMods;
    QHash<PackageId, ModDiagnostics> m_diagnostics;
    QHash<PackageId, AboutXmlModPtr> m_activeMods;
    QHash<QString, PackageId> m_uuidToPid;
    QList<PackageId> m_orderedPids;

    QSet<PackageId> m_noVersionWarning;
    QHash<QString, ReplacementInfo> m_useThisInstead;
    sort::CommunityRules m_communityRules;
    QList<QList<PackageId>> m_cachedCycles;
    // Per-mod issues from each checker (checker order), reused by reorder() for
    // order-independent checkers; nullopt when stale.
    std::optional<CheckerResults> m_checkerResults;
};

}  // namespace px::modrim::checker
