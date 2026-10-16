#include "core/services/diagnostics_service.h"

#include <memory>
#include <utility>
#include <vector>

#include <QCoro/QCoroFuture>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QtConcurrent/QtConcurrentRun>

#include "core/config.h"
#include "core/context.h"
#include "core/sort/community.h"

namespace px::modrim {
namespace {

// Cached databases older than this are refreshed in the background.
constexpr double kDatabaseMaxAgeS = 24 * 3600;

bool needs_refresh(bool loaded, const std::optional<double>& age) {
    return !loaded || !age || *age > kDatabaseMaxAgeS;
}

std::vector<std::unique_ptr<checker::ModIssueChecker>> default_checkers() {
    std::vector<std::unique_ptr<checker::ModIssueChecker>> checkers;
    checkers.push_back(std::make_unique<checker::DependencyIssueChecker>());
    checkers.push_back(std::make_unique<checker::IncompatibilityIssueChecker>());
    checkers.push_back(std::make_unique<checker::LoadOrderIssueChecker>());
    checkers.push_back(std::make_unique<checker::CycleIssueChecker>());
    checkers.push_back(std::make_unique<checker::GameVersionIssueChecker>());
    checkers.push_back(std::make_unique<checker::ReplacementIssueChecker>());
    return checkers;
}

view::SidebarEntry make_entry(view::SidebarEntry::Kind kind,
                              QString label,
                              QSet<QString> visible_uuids,
                              QString provider_id = {}) {
    view::SidebarEntry entry;
    entry.kind = kind;
    entry.label = std::move(label);
    entry.visible_uuids = std::move(visible_uuids);
    entry.provider_id = std::move(provider_id);
    return entry;
}

}  // namespace

DiagnosticsService::DiagnosticsService(CoreContext& ctx, QNetworkAccessManager& network)
    : ctx_(ctx),
      no_version_warning_service_(ctx.configService(), network),
      use_this_instead_service_(ctx.configService(), network),
      checker_(default_checkers(),
               ctx.config().sort,
               ctx.targetVersion(),
               [this](const checker::DiagnosticsByUuid& diagnostics) {
                   on_checker_diagnostics_changed(diagnostics);
               }) {}

// ── Lifecycle ──────────────────────────────────────────────

QCoro::Task<Result<void>> DiagnosticsService::initialize(Profiler* timer) {
    Profiler local;
    Profiler& t = timer != nullptr ? *timer : local;
    auto init_scope = t("diagnostics.init");
    {
        auto scope = t("ensure_databases");
        co_await ensure_databases();
    }

    std::optional<sort::CommunityRules> rules;
    {
        auto scope = t("load_community_rules");
        if (const auto path = community_rules_path()) {
            auto loaded = co_await QtConcurrent::run(
                [p = *path] { return sort::load_community_rules(p); });
            if (!loaded) {
                co_return std::unexpected(loaded.error());
            }
            rules = std::move(*loaded);
        }
    }
    if (rules && !rules->isEmpty()) {
        checker_.set_community_rules(std::move(*rules));
    }
    co_return {};
}

QCoro::Task<void> DiagnosticsService::ensure_databases() {
    // The cache files are read off the UI thread; the service is only touched from here.
    auto nvw = co_await QtConcurrent::run(
        [this] { return no_version_warning_service_.load_if_exists(); });
    const bool nvw_loaded = !nvw.isEmpty();
    if (nvw_loaded) {
        checker_.set_no_version_warning(std::move(nvw));
    }
    auto uti = co_await QtConcurrent::run(
        [this] { return use_this_instead_service_.load_if_exists(); });
    const bool uti_loaded = !uti.isEmpty();
    if (uti_loaded) {
        checker_.set_use_this_instead(std::move(uti));
    }

    const bool refresh_nvw =
        needs_refresh(nvw_loaded, no_version_warning_service_.cache_age_s());
    const bool refresh_uti = needs_refresh(uti_loaded, use_this_instead_service_.cache_age_s());
    if ((refresh_nvw || refresh_uti) && (!db_task_ || db_task_->isReady())) {
        db_task_ = download_databases(refresh_nvw, refresh_uti);
    }
}

QCoro::Task<void> DiagnosticsService::download_databases(bool nvw, bool uti) {
    background_task_changed.notify(QStringLiteral(u"Downloading rule databases\u2026"));
    bool changed = false;
    if (nvw) {
        auto pids = co_await no_version_warning_service_.ensure(true);
        if (!pids.isEmpty()) {
            checker_.set_no_version_warning(std::move(pids));
            changed = true;
        }
    }
    if (uti) {
        auto entries = co_await use_this_instead_service_.ensure(true);
        if (!entries.isEmpty()) {
            checker_.set_use_this_instead(std::move(entries));
            changed = true;
        }
    }
    if (changed) {
        rebuild();
    }
    background_task_changed.notify(QString{});
}

std::optional<QString> DiagnosticsService::community_rules_path() const {
    const AppConfig& config = ctx_.config();
    if (!config.sort.use_community_rules) {
        return std::nullopt;
    }
    QString path = QString::fromStdString(config.paths.community_rules_file);
    if (path.isEmpty()) {
        path = QDir(ctx_.configService().directory()).filePath(QStringLiteral("communityRules.json"));
    }
    if (!QFileInfo::exists(path)) {
        return std::nullopt;
    }
    return path;
}

// ── Mutations ──────────────────────────────────────────────

void DiagnosticsService::rebuild(Profiler* timer) {
    rebuild(ctx_.activeUuids(), timer);
}

void DiagnosticsService::rebuild(const QStringList& active_uuids, Profiler* timer) {
    last_active_uuids_ = active_uuids;
    Profiler local;
    Profiler& t = timer != nullptr ? *timer : local;
    auto scope = t("diagnostics.rebuild");
    checker_.rebuild(ctx_.allMods(), active_uuids, &t);
}

void DiagnosticsService::reorder(const QStringList& active_uuids) {
    last_active_uuids_ = active_uuids;
    checker_.reorder(active_uuids);
}

// ── Queries ────────────────────────────────────────────────

QHash<checker::PackageId, checker::AboutXmlModPtr> DiagnosticsService::active_mods_by_pid() const {
    return checker_.active_mods();
}

const checker::ConstraintGraph& DiagnosticsService::constraint_graph() const {
    return checker_.graph();
}

QList<checker::PackageId> DiagnosticsService::active_ordered_pids() const {
    return checker_.ordered_pids();
}

const sort::CommunityRules& DiagnosticsService::community_rules() const {
    return checker_.community_rules();
}

QList<view::ModIssueView> DiagnosticsService::issues_for(const QString& uuid) const {
    const checker::ModDiagnostics* diag = checker_.diagnostics_for(uuid);
    return diag != nullptr ? to_issue_views(*diag) : QList<view::ModIssueView>{};
}

std::optional<view::ModDiagnosticsView> DiagnosticsService::summary_for(const QString& uuid) const {
    const auto it = last_summary_.constFind(uuid);
    if (it == last_summary_.cend()) {
        return std::nullopt;
    }
    return it.value();
}

int DiagnosticsService::active_error_count() const {
    const auto diagnostics = checker_.active_mod_diagnostics();
    int count = 0;
    for (const auto& [uuid, diag] : diagnostics.asKeyValueRange()) {
        count += diag.has_errors() ? 1 : 0;
    }
    return count;
}

// ── Conversions ────────────────────────────────────────────

QList<view::ModIssueView> DiagnosticsService::to_issue_views(const checker::ModDiagnostics& diag) {
    QList<view::ModIssueView> views;
    const auto append = [&views](const QList<checker::ModIssue>& issues, bool is_error) {
        for (const checker::ModIssue& issue : issues) {
            view::ModIssueView view;
            view.category = issue.category;
            view.category_display_name =
                issue.category_display_name.isEmpty() ? issue.category : issue.category_display_name;
            if (!issue.detail_message.isEmpty()) {
                view.detail = issue.detail_message;
            }
            view.is_error = is_error;
            views.append(std::move(view));
        }
    };
    append(diag.errors, true);
    append(diag.warnings, false);
    return views;
}

view::ModDiagnosticsView DiagnosticsService::to_view(const checker::ModDiagnostics& diag) {
    view::ModDiagnosticsView view;
    view.has_errors = diag.has_errors();
    view.has_warnings = diag.has_warnings();
    view.error_tooltip = diag.error_tooltip();
    view.warning_tooltip = diag.warning_tooltip();
    return view;
}

// ── Checker callback ───────────────────────────────────────

void DiagnosticsService::on_checker_diagnostics_changed(
    const checker::DiagnosticsByUuid& diagnostics) {
    QHash<QString, view::ModDiagnosticsView> summary;
    summary.reserve(diagnostics.size());
    for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it) {
        summary.insert(it.key(), to_view(it.value()));
    }
    last_summary_ = std::move(summary);
    // Sidebar first: the summary handler re-applies the current sidebar filter and must see the
    // entries for this state, not the previous one.
    sidebar_entries_changed.notify(build_sidebar_entries());
    diagnostics_summary_changed.notify(last_summary_);
    status_message_changed.notify(format_status());
}

// ── Status ─────────────────────────────────────────────────

QString DiagnosticsService::format_status() const {
    const auto diagnostics = checker_.active_mod_diagnostics();
    int err_count = 0;
    int warn_count = 0;
    for (const auto& [uuid, diag] : diagnostics.asKeyValueRange()) {
        err_count += diag.has_errors() ? 1 : 0;
        warn_count += diag.has_warnings() ? 1 : 0;
    }

    QStringList parts{QString::number(last_active_uuids_.size()) + QStringLiteral(" active")};
    if (err_count > 0) {
        parts.append(QString::number(err_count) + QStringLiteral(" with errors"));
    }
    if (warn_count > 0) {
        parts.append(QString::number(warn_count) + QStringLiteral(" with warnings"));
    }
    return parts.join(QStringLiteral(" | "));
}

// ── Sidebar ────────────────────────────────────────────────

QList<view::SidebarEntry> DiagnosticsService::build_sidebar_entries() {
    const QHash<QString, ListedModPtr>& mods = ctx_.allMods();
    if (!sidebar_base_ || !sidebar_base_->matches(mods)) {
        sidebar_base_.emplace(mods);
    }
    const SidebarBase& base = *sidebar_base_;

    QSet<QString> errors = base.invalid;
    QSet<QString> warnings;
    const auto diagnostics = checker_.active_mod_diagnostics();
    for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it) {
        if (!mods.contains(it.key())) {
            continue;
        }
        if (it.value().has_errors()) {
            errors.insert(it.key());
        }
        if (it.value().has_warnings()) {
            warnings.insert(it.key());
        }
    }

    const QSet<QString> active(last_active_uuids_.cbegin(), last_active_uuids_.cend());
    QSet<QString> inactive = base.all_uuids;
    inactive.subtract(active);

    using Kind = view::SidebarEntry::Kind;
    QList<view::SidebarEntry> entries{
        make_entry(Kind::All, QStringLiteral("All"), base.all_uuids),
        make_entry(Kind::Active, QStringLiteral("Active"), active),
        make_entry(Kind::Inactive, QStringLiteral("Inactive"), std::move(inactive)),
        make_entry(Kind::WithErrors, QStringLiteral("With errors"), std::move(errors)),
        make_entry(Kind::WithWarnings, QStringLiteral("With warnings"), std::move(warnings)),
    };
    for (auto it = base.by_provider.cbegin(); it != base.by_provider.cend(); ++it) {
        entries.append(make_entry(Kind::Provider,
                                  view::provider_label(it.key()),
                                  it.value(),
                                  it.key()));
    }
    return entries;
}

DiagnosticsService::SidebarBase::SidebarBase(const QHash<QString, ListedModPtr>& current)
    : mods(current) {
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        all_uuids.insert(it.key());
        by_provider[it.value()->provider_id].insert(it.key());
        if (!it.value()->valid) {
            invalid.insert(it.key());
        }
    }
}

// Compares by mod identity: the same uuids mapping to the same objects.
bool DiagnosticsService::SidebarBase::matches(const QHash<QString, ListedModPtr>& current) const {
    if (mods.size() != current.size()) {
        return false;
    }
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        const auto cached = mods.constFind(it.key());
        if (cached == mods.cend() || cached.value() != it.value()) {
            return false;
        }
    }
    return true;
}

}  // namespace px::modrim
