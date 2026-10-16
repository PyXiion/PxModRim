#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

#include <QCoro/QCoroTask>

#include "core/checker/checker.hpp"
#include "core/checker/databases.hpp"
#include "core/checker/graph.hpp"
#include "core/checker/models.hpp"
#include "core/error.hpp"
#include "core/events.hpp"
#include "core/models/view/diagnostics.hpp"
#include "core/models/view/sidebar.hpp"
#include "core/profiler.hpp"
#include "core/sort/models.hpp"

class QNetworkAccessManager;

namespace px::modrim {

class CoreContext;

// Keeps the checker in step with the active mods and the rule databases, and
// publishes the diagnostics summary, the sidebar filters and the status line
// derived from it.
class DiagnosticsService {
 public:
  // `network` is not owned and must outlive the service.
  DiagnosticsService(CoreContext& ctx, QNetworkAccessManager& network);

  Event<QHash<QString, view::ModDiagnosticsView>> diagnosticsSummaryChanged;
  Event<QString> statusMessageChanged;
  Event<QString> backgroundTaskChanged;
  Event<QList<view::SidebarEntry>> sidebarEntriesChanged;

  // ── Lifecycle ─────────────────────────────────────────────

  // Applies cached databases and community rules, and starts a background
  // refresh of stale databases. Fails when the community rules file is
  // malformed.
  QCoro::Task<Result<void>> initialize(Profiler* timer = nullptr);

  // ── Mutations ─────────────────────────────────────────────

  // Rebuilds the checker for the context's active mods, or for `active_uuids`.
  void rebuild(Profiler* timer = nullptr);
  void rebuild(const QStringList& activeUuids, Profiler* timer = nullptr);

  // Notifies the checker of a new active mod order.
  void reorder(const QStringList& activeUuids);

  // ── Queries ───────────────────────────────────────────────

  QHash<checker::PackageId, checker::AboutXmlModPtr> activeModsByPid() const;
  const checker::ConstraintGraph& constraintGraph() const;
  QList<checker::PackageId> activeOrderedPids() const;
  const sort::CommunityRules& communityRules() const;

  // Views of every issue on a mod; empty when the mod is not active.
  QList<view::ModIssueView> issuesFor(const QString& uuid) const;
  std::optional<view::ModDiagnosticsView> summaryFor(const QString& uuid) const;
  int activeErrorCount() const;

 private:
  // Partitions that do not depend on diagnostics; reused until the mod list
  // changes.
  struct SidebarBase {
    QHash<QString, ListedModPtr> mods;
    QSet<QString> allUuids;
    QSet<QString> invalid;
    QMap<QString, QSet<QString>> byProvider;

    explicit SidebarBase(const QHash<QString, ListedModPtr>& current);
    bool matches(const QHash<QString, ListedModPtr>& current) const;
  };

  QCoro::Task<void> ensureDatabases();
  QCoro::Task<void> downloadDatabases(bool nvw, bool uti);
  // The community rules file to load, or nullopt when rules are disabled or no
  // file exists.
  std::optional<QString> communityRulesPath() const;

  static QList<view::ModIssueView> toIssueViews(
      const checker::ModDiagnostics& diag);
  static view::ModDiagnosticsView toView(const checker::ModDiagnostics& diag);

  void onCheckerDiagnosticsChanged(
      const checker::DiagnosticsByUuid& diagnostics);
  QString formatStatus() const;
  QList<view::SidebarEntry> buildSidebarEntries();

  CoreContext& m_ctx;
  checker::NoVersionWarningService m_noVersionWarningService;
  checker::UseThisInsteadService m_useThisInsteadService;
  checker::ModChecker m_checker;
  std::optional<QCoro::Task<void>> m_dbTask;
  QStringList m_lastActiveUuids;
  QHash<QString, view::ModDiagnosticsView> m_lastSummary;
  std::optional<SidebarBase> m_sidebarBase;
};

}  // namespace px::modrim
