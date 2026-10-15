#pragma once

#include <optional>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/checker/checker.h"
#include "core/checker/databases.h"
#include "core/checker/graph.h"
#include "core/checker/models.h"
#include "core/error.h"
#include "core/events.h"
#include "core/models/view/diagnostics.h"
#include "core/models/view/sidebar.h"
#include "core/profiler.h"
#include "core/sort/models.h"

class QNetworkAccessManager;

namespace px::core {

class CoreContext;

// Keeps the checker in step with the active mods and the rule databases, and publishes the
// diagnostics summary, the sidebar filters and the status line derived from it.
class DiagnosticsService {
public:
    // `network` is not owned and must outlive the service.
    DiagnosticsService(CoreContext& ctx, QNetworkAccessManager& network);

    Event<QHash<QString, view::ModDiagnosticsView>> diagnostics_summary_changed;
    Event<QString> status_message_changed;
    Event<QString> background_task_changed;
    Event<QList<view::SidebarEntry>> sidebar_entries_changed;

    // ── Lifecycle ─────────────────────────────────────────────

    // Applies cached databases and community rules, and starts a background refresh of stale
    // databases. Fails when the community rules file is malformed.
    QCoro::Task<Result<void>> initialize(Profiler* timer = nullptr);

    // ── Mutations ─────────────────────────────────────────────

    // Rebuilds the checker for the context's active mods, or for `active_uuids`.
    void rebuild(Profiler* timer = nullptr);
    void rebuild(const QStringList& active_uuids, Profiler* timer = nullptr);

    // Notifies the checker of a new active mod order.
    void reorder(const QStringList& active_uuids);

    // ── Queries ───────────────────────────────────────────────

    QHash<checker::PackageId, checker::AboutXmlModPtr> active_mods_by_pid() const;
    const checker::ConstraintGraph& constraint_graph() const;
    QList<checker::PackageId> active_ordered_pids() const;
    const sort::CommunityRules& community_rules() const;

    // Views of every issue on a mod; empty when the mod is not active.
    QList<view::ModIssueView> issues_for(const QString& uuid) const;
    std::optional<view::ModDiagnosticsView> summary_for(const QString& uuid) const;
    int active_error_count() const;

private:
    // Partitions that do not depend on diagnostics; reused until the mod list changes.
    struct SidebarBase {
        QHash<QString, ListedModPtr> mods;
        QSet<QString> all_uuids;
        QSet<QString> invalid;
        QMap<QString, QSet<QString>> by_provider;

        explicit SidebarBase(const QHash<QString, ListedModPtr>& current);
        bool matches(const QHash<QString, ListedModPtr>& current) const;
    };

    QCoro::Task<void> ensure_databases();
    QCoro::Task<void> download_databases(bool nvw, bool uti);
    // The community rules file to load, or nullopt when rules are disabled or no file exists.
    std::optional<QString> community_rules_path() const;

    static QList<view::ModIssueView> to_issue_views(const checker::ModDiagnostics& diag);
    static view::ModDiagnosticsView to_view(const checker::ModDiagnostics& diag);

    void on_checker_diagnostics_changed(const checker::DiagnosticsByUuid& diagnostics);
    QString format_status() const;
    QList<view::SidebarEntry> build_sidebar_entries();

    CoreContext& ctx_;
    checker::NoVersionWarningService no_version_warning_service_;
    checker::UseThisInsteadService use_this_instead_service_;
    checker::ModChecker checker_;
    std::optional<QCoro::Task<void>> db_task_;
    QStringList last_active_uuids_;
    QHash<QString, view::ModDiagnosticsView> last_summary_;
    std::optional<SidebarBase> sidebar_base_;
};

}  // namespace px::core
