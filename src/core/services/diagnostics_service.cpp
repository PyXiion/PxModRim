#include "core/services/diagnostics_service.hpp"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <memory>
#include <utility>
#include <vector>

#include <QCoro/QCoroFuture>
#include <QtConcurrent/QtConcurrentRun>

#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/context.hpp"
#include "core/providers/labels.hpp"
#include "core/sort/community.hpp"

namespace px::modrim {
namespace {

// Cached databases older than this are refreshed in the background.
constexpr double kDatabaseMaxAgeS = 24 * 3600;

bool needsRefresh(bool loaded, const std::optional<double>& age) {
  return !loaded || !age || *age > kDatabaseMaxAgeS;
}

std::vector<std::unique_ptr<checker::ModIssueChecker>> defaultCheckers() {
  std::vector<std::unique_ptr<checker::ModIssueChecker>> checkers;
  checkers.push_back(std::make_unique<checker::DependencyIssueChecker>());
  checkers.push_back(std::make_unique<checker::IncompatibilityIssueChecker>());
  checkers.push_back(std::make_unique<checker::LoadOrderIssueChecker>());
  checkers.push_back(std::make_unique<checker::CycleIssueChecker>());
  checkers.push_back(std::make_unique<checker::GameVersionIssueChecker>());
  checkers.push_back(std::make_unique<checker::ReplacementIssueChecker>());
  return checkers;
}

view::SidebarEntry makeEntry(view::SidebarEntry::Kind kind, QString label,
                             QSet<QString> visibleUuids) {
  view::SidebarEntry entry;
  entry.kind = kind;
  entry.label = std::move(label);
  entry.visibleUuids = std::move(visibleUuids);
  return entry;
}

}  // namespace

DiagnosticsService::DiagnosticsService(CoreContext& ctx,
                                       QNetworkAccessManager& network)
    : m_ctx(ctx),
      m_noVersionWarningService(ctx.configService(), network),
      m_useThisInsteadService(ctx.configService(), network),
      m_checker(defaultCheckers(), ctx.config().sort, ctx.targetVersion(),
                [this](const checker::DiagnosticsByUuid& diagnostics) {
                  onCheckerDiagnosticsChanged(diagnostics);
                }) {}

QCoro::Task<void> DiagnosticsService::initialize(Profiler* timer) {
  Profiler local;
  Profiler& t = timer != nullptr ? *timer : local;
  auto initScope = t("diagnostics.init");
  {
    auto scope = t("ensure_databases");
    co_await ensureDatabases();
  }

  sort::CommunityRules rules;
  {
    auto scope = t("load_community_rules");
    if (const auto path = communityRulesPath()) {
      auto loaded = co_await QtConcurrent::run(
          [p = *path] { return sort::loadCommunityRules(p); });
      if (loaded) {
        rules = std::move(*loaded);
      } else {
        spdlog::warn("Ignoring community rules: {}",
                     loaded.error().message.toStdString());
        warningRaised.notify(
            QStringLiteral("Community rules are invalid and were ignored"));
      }
    }
  }
  // Applied even when empty, so rules that are no longer configured, missing,
  // unreadable or empty stop influencing diagnostics and sorting.
  m_checker.setCommunityRules(std::move(rules));
  m_ctx.invalidateSortInputs();
}

QCoro::Task<void> DiagnosticsService::ensureDatabases() {
  // The cache files are read off the UI thread; the service is only touched
  // from here.
  auto nvw = co_await QtConcurrent::run(
      [this] { return m_noVersionWarningService.loadIfExists(); });
  const bool nvwLoaded = !nvw.isEmpty();
  if (nvwLoaded) {
    m_checker.setNoVersionWarning(std::move(nvw));
  }
  auto uti = co_await QtConcurrent::run(
      [this] { return m_useThisInsteadService.loadIfExists(); });
  const bool utiLoaded = !uti.isEmpty();
  if (utiLoaded) {
    m_checker.setUseThisInstead(std::move(uti));
  }

  const bool refreshNvw =
      needsRefresh(nvwLoaded, m_noVersionWarningService.cacheAgeS());
  const bool refreshUti =
      needsRefresh(utiLoaded, m_useThisInsteadService.cacheAgeS());
  if ((refreshNvw || refreshUti) && (!m_dbTask || m_dbTask->isReady())) {
    m_dbTask = downloadDatabases(refreshNvw, refreshUti);
  }
}

QCoro::Task<void> DiagnosticsService::downloadDatabases(bool nvw, bool uti) {
  backgroundTaskChanged.notify(
      QStringLiteral(u"Downloading rule databases\u2026"));
  bool changed = false;
  if (nvw) {
    auto pids = co_await m_noVersionWarningService.ensure(true);
    if (!pids.isEmpty()) {
      m_checker.setNoVersionWarning(std::move(pids));
      changed = true;
    }
  }
  if (uti) {
    auto entries = co_await m_useThisInsteadService.ensure(true);
    if (!entries.isEmpty()) {
      m_checker.setUseThisInstead(std::move(entries));
      changed = true;
    }
  }
  if (changed) {
    rebuild();
  }
  backgroundTaskChanged.notify(QString{});
}

std::optional<QString> DiagnosticsService::communityRulesPath() const {
  const AppConfig& config = m_ctx.config();
  if (!config.sort.use_community_rules) {
    return std::nullopt;
  }
  QString path = QString::fromStdString(config.paths.community_rules_file);
  if (path.isEmpty()) {
    path = QDir(m_ctx.configService().directory())
               .filePath(QStringLiteral("communityRules.json"));
  }
  if (!QFileInfo::exists(path)) {
    return std::nullopt;
  }
  return path;
}

void DiagnosticsService::rebuild(Profiler* timer) {
  rebuild(m_ctx.activeUuids(), timer);
}

void DiagnosticsService::rebuild(const QStringList& activeUuids,
                                 Profiler* timer) {
  m_lastActiveUuids = activeUuids;
  Profiler local;
  Profiler& t = timer != nullptr ? *timer : local;
  auto scope = t("diagnostics.rebuild");
  m_checker.rebuild(m_ctx.allMods(), activeUuids, &t);
}

void DiagnosticsService::reorder(const QStringList& activeUuids) {
  m_lastActiveUuids = activeUuids;
  m_checker.reorder(activeUuids);
}

QHash<checker::PackageId, checker::AboutXmlModPtr>
DiagnosticsService::activeModsByPid() const {
  return m_checker.activeMods();
}

const checker::ConstraintGraph& DiagnosticsService::constraintGraph() const {
  return m_checker.graph();
}

const sort::CommunityRules& DiagnosticsService::communityRules() const {
  return m_checker.communityRules();
}

QList<view::ModIssueView> DiagnosticsService::issuesFor(
    const QString& uuid) const {
  const checker::ModDiagnostics* diag = m_checker.diagnosticsFor(uuid);
  return diag != nullptr ? toIssueViews(*diag) : QList<view::ModIssueView>{};
}

std::optional<view::ModDiagnosticsView> DiagnosticsService::summaryFor(
    const QString& uuid) const {
  const auto it = m_lastSummary.constFind(uuid);
  if (it == m_lastSummary.cend()) {
    return std::nullopt;
  }
  return it.value();
}

DiagnosticsService::ActiveCounts DiagnosticsService::activeCounts() const {
  const auto diagnostics = m_checker.activeModDiagnostics();
  ActiveCounts counts;
  for (const auto& [uuid, diag] : diagnostics.asKeyValueRange()) {
    counts.errors += diag.hasErrors() ? 1 : 0;
    counts.warnings += diag.hasWarnings() ? 1 : 0;
  }
  return counts;
}

int DiagnosticsService::activeErrorCount() const {
  return activeCounts().errors;
}

QList<view::ModIssueView> DiagnosticsService::toIssueViews(
    const checker::ModDiagnostics& diag) {
  QList<view::ModIssueView> views;
  const auto append = [&views](const QList<checker::ModIssue>& issues,
                               bool isError) {
    for (const checker::ModIssue& issue : issues) {
      view::ModIssueView view;
      view.categoryDisplayName = issue.categoryDisplayName;
      if (!issue.detailMessage.isEmpty()) {
        view.detail = issue.detailMessage;
      }
      view.isError = isError;
      views.append(std::move(view));
    }
  };
  append(diag.errors, true);
  append(diag.warnings, false);
  return views;
}

view::ModDiagnosticsView DiagnosticsService::toView(
    const checker::ModDiagnostics& diag) {
  view::ModDiagnosticsView view;
  view.hasErrors = diag.hasErrors();
  view.hasWarnings = diag.hasWarnings();
  view.errorTooltip = diag.errorTooltip();
  view.warningTooltip = diag.warningTooltip();
  return view;
}

void DiagnosticsService::onCheckerDiagnosticsChanged(
    const checker::DiagnosticsByUuid& diagnostics) {
  QHash<QString, view::ModDiagnosticsView> summary;
  summary.reserve(diagnostics.size());
  for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it) {
    summary.insert(it.key(), toView(it.value()));
  }
  m_lastSummary = std::move(summary);
  // Sidebar first: the summary handler re-applies the current sidebar filter
  // and must see the entries for this state, not the previous one.
  sidebarEntriesChanged.notify(buildSidebarEntries());
  diagnosticsSummaryChanged.notify(m_lastSummary);
  statusMessageChanged.notify(formatStatus());
}

QString DiagnosticsService::formatStatus() const {
  const auto [errCount, warnCount] = activeCounts();

  QStringList parts{QString::number(m_lastActiveUuids.size()) +
                    QStringLiteral(" active")};
  if (errCount > 0) {
    parts.append(QString::number(errCount) + QStringLiteral(" with errors"));
  }
  if (warnCount > 0) {
    parts.append(QString::number(warnCount) + QStringLiteral(" with warnings"));
  }
  return parts.join(QStringLiteral(" | "));
}

QList<view::SidebarEntry> DiagnosticsService::buildSidebarEntries() {
  const QHash<QString, ListedModPtr>& mods = m_ctx.allMods();

  QSet<QString> allUuids;
  QSet<QString> errors;
  QSet<QString> warnings;
  QMap<QString, QSet<QString>> byProvider;
  for (auto it = mods.cbegin(); it != mods.cend(); ++it) {
    allUuids.insert(it.key());
    byProvider[it.value()->providerId].insert(it.key());
    if (!it.value()->valid) {
      errors.insert(it.key());
    }
  }

  const auto diagnostics = m_checker.activeModDiagnostics();
  for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it) {
    if (!mods.contains(it.key())) {
      continue;
    }
    if (it.value().hasErrors()) {
      errors.insert(it.key());
    }
    if (it.value().hasWarnings()) {
      warnings.insert(it.key());
    }
  }

  const QSet<QString> active(m_lastActiveUuids.cbegin(),
                             m_lastActiveUuids.cend());
  QSet<QString> inactive = allUuids;
  inactive.subtract(active);

  using Kind = view::SidebarEntry::Kind;
  QList<view::SidebarEntry> entries{
      makeEntry(Kind::kAll, QStringLiteral("All"), std::move(allUuids)),
      makeEntry(Kind::kActive, QStringLiteral("Active"), active),
      makeEntry(Kind::kInactive, QStringLiteral("Inactive"),
                std::move(inactive)),
      makeEntry(Kind::kWithErrors, QStringLiteral("With errors"),
                std::move(errors)),
      makeEntry(Kind::kWithWarnings, QStringLiteral("With warnings"),
                std::move(warnings)),
  };
  for (auto it = byProvider.cbegin(); it != byProvider.cend(); ++it) {
    entries.append(
        makeEntry(Kind::kProvider, providerLabel(it.key()), it.value()));
  }
  return entries;
}

}  // namespace px::modrim
