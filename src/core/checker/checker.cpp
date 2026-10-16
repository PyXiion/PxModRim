#include "core/checker/checker.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

namespace px::modrim::checker {
namespace {

ModDiagnostics compose(const std::vector<QList<ModIssue>>& per_checker) {
    ModDiagnostics diagnostics;
    for (const auto& issues : per_checker) {
        for (const ModIssue& issue : issues) {
            if (issue.severity == IssueSeverity::Error) {
                diagnostics.errors.append(issue);
            } else {
                diagnostics.warnings.append(issue);
            }
        }
    }
    return diagnostics;
}

// Same key set, and every entry is the same mod object (identity, not equality).
bool same_mods(const QHash<PackageId, AboutXmlModPtr>& a,
               const QHash<PackageId, AboutXmlModPtr>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (auto it = a.cbegin(); it != a.cend(); ++it) {
        const auto other = b.constFind(it.key());
        if (other == b.cend() || other.value() != it.value()) {
            return false;
        }
    }
    return true;
}

}  // namespace

ModChecker::ModChecker(std::vector<std::unique_ptr<ModIssueChecker>> checkers,
                       sort::SortSettings settings,
                       QString target_version,
                       DiagnosticsCallback on_diagnostics_changed)
    : checkers_(std::move(checkers)),
      settings_(std::move(settings)),
      target_version_(std::move(target_version)),
      on_diagnostics_changed_(std::move(on_diagnostics_changed)) {}

void ModChecker::set_no_version_warning(QSet<PackageId> pids) {
    no_version_warning_ = std::move(pids);
    checker_results_.reset();
}

void ModChecker::set_use_this_instead(QHash<QString, ReplacementInfo> db) {
    use_this_instead_ = std::move(db);
    checker_results_.reset();
}

void ModChecker::set_community_rules(sort::CommunityRules rules) {
    community_rules_ = std::move(rules);
}

void ModChecker::rebuild(const QHash<QString, ListedModPtr>& mods,
                         const QStringList& ordered_uuids,
                         Profiler* timer) {
    Profiler local;
    Profiler& t = timer != nullptr ? *timer : local;

    {
        auto scope = t("collect_active");
        collect_active(mods, ordered_uuids);
    }
    {
        auto scope = t("graph.build");
        graph_.build(active_mods_, ordered_pids_, settings_, &community_rules_);
    }
    {
        auto scope = t("find_cycles");
        cached_cycles_ = graph_.find_cycles();
    }
    const CheckContext ctx = build_context(cached_cycles_);
    {
        auto scope = t("check_mod");
        check_all(ctx);
    }

    emit_diagnostics();
}

void ModChecker::toggle_mod(const ListedMod& mod, const QStringList& ordered_uuids) {
    if (dynamic_cast<const AboutXmlMod*>(&mod) == nullptr) {
        emit_diagnostics();
        return;
    }
    rebuild(all_mods_, ordered_uuids);
}

void ModChecker::move_mod(const QString& uuid, int old_index, int new_index) {
    if (ordered_pids_.isEmpty()) {
        return;
    }
    const auto uuid_it = uuid_to_pid_.constFind(uuid);
    if (uuid_it == uuid_to_pid_.cend()) {
        return;
    }
    const PackageId pid = uuid_it.value();

    ordered_pids_.insert(new_index, ordered_pids_.takeAt(old_index));
    graph_.update_order(ordered_pids_);

    const CheckContext ctx = build_context(cached_cycles_);
    // Only neighbours are rechecked, so cached results no longer match the order.
    checker_results_.reset();

    QSet<PackageId> affected{pid};
    affected.unite(graph_.neighbors(pid));
    for (const PackageId& current_pid : affected) {
        if (const auto mod = active_mods_.constFind(current_pid); mod != active_mods_.cend()) {
            diagnostics_.insert(current_pid, check_mod(*mod.value(), ctx));
        }
    }

    emit_diagnostics();
}

void ModChecker::reorder(const QStringList& ordered_uuids) {
    const QHash<PackageId, AboutXmlModPtr> previous = active_mods_;
    const QHash<PackageId, int> previous_index = graph_.pid_to_index();
    collect_active(all_mods_, ordered_uuids);
    graph_.update_order(ordered_pids_);

    const QList<QList<PackageId>> cycles = graph_.find_cycles();
    const CheckContext ctx = build_context(cycles);

    if (!checker_results_.has_value() || !same_mods(previous, active_mods_)) {
        check_all(ctx);
    } else {
        QSet<PackageId> moved;
        for (auto it = ctx.pid_to_index.cbegin(); it != ctx.pid_to_index.cend(); ++it) {
            if (previous_index.value(it.key(), -1) != it.value()) {
                moved.insert(it.key());
            }
        }

        CheckerResults& results = *checker_results_;
        QSet<PackageId> changed;
        for (std::size_t i = 0; i < checkers_.size(); ++i) {
            const ModIssueChecker& checker = *checkers_[i];
            if (!checker.order_dependent()) {
                continue;
            }
            const auto affected = checker.affected_by_moves(ctx, moved);
            for (auto it = active_mods_.cbegin(); it != active_mods_.cend(); ++it) {
                if (affected && !affected->contains(it.key())) {
                    continue;
                }
                QList<ModIssue> issues = run_checker(checker, *it.value(), ctx);
                auto& stored = results[it.key()][i];
                if (issues != stored) {
                    stored = std::move(issues);
                    changed.insert(it.key());
                }
            }
        }
        for (const PackageId& pid : changed) {
            diagnostics_.insert(pid, compose(results[pid]));
        }
    }

    emit_diagnostics();
}

const ModDiagnostics* ModChecker::diagnostics_for(const QString& uuid) const {
    const auto pid = uuid_to_pid_.constFind(uuid);
    if (pid == uuid_to_pid_.cend()) {
        return nullptr;
    }
    const auto diagnostics = diagnostics_.constFind(pid.value());
    return diagnostics == diagnostics_.cend() ? nullptr : &diagnostics.value();
}

DiagnosticsByUuid ModChecker::active_mod_diagnostics() const {
    DiagnosticsByUuid result;
    for (auto it = uuid_to_pid_.cbegin(); it != uuid_to_pid_.cend(); ++it) {
        if (const auto diagnostics = diagnostics_.constFind(it.value());
            diagnostics != diagnostics_.cend()) {
            result.insert(it.key(), diagnostics.value());
        }
    }
    return result;
}

void ModChecker::collect_active(const QHash<QString, ListedModPtr>& mods,
                                const QStringList& ordered_uuids) {
    // `mods` may alias all_mods_ (toggle_mod and reorder pass it back in), so copy first.
    all_mods_ = mods;
    active_mods_.clear();
    uuid_to_pid_.clear();
    ordered_pids_.clear();

    for (const QString& uuid : ordered_uuids) {
        const auto listed = all_mods_.constFind(uuid);
        if (listed == all_mods_.cend()) {
            continue;
        }
        auto about = std::dynamic_pointer_cast<AboutXmlMod>(listed.value());
        if (!about) {
            continue;
        }
        const PackageId pid = about->package_id;
        active_mods_.insert(pid, about);
        uuid_to_pid_.insert(uuid, pid);
        ordered_pids_.append(pid);
    }
}

CheckContext ModChecker::build_context(const QList<QList<PackageId>>& cycles) const {
    return CheckContext{
        .active_mods = active_mods_,
        .ordered_pids = ordered_pids_,
        .pid_to_index = graph_.pid_to_index(),
        .graph = graph_,
        .settings = settings_,
        .target_version = target_version_,
        .no_version_warning = no_version_warning_,
        .use_this_instead = use_this_instead_,
        .cycles = cycles,
    };
}

void ModChecker::check_all(const CheckContext& ctx) {
    CheckerResults results;
    QHash<PackageId, ModDiagnostics> diagnostics;
    for (auto it = active_mods_.cbegin(); it != active_mods_.cend(); ++it) {
        std::vector<QList<ModIssue>> per_checker;
        per_checker.reserve(checkers_.size());
        for (const auto& checker : checkers_) {
            per_checker.push_back(run_checker(*checker, *it.value(), ctx));
        }
        diagnostics.insert(it.key(), compose(per_checker));
        results.insert(it.key(), std::move(per_checker));
    }
    checker_results_ = std::move(results);
    diagnostics_ = std::move(diagnostics);
}

ModDiagnostics ModChecker::check_mod(const AboutXmlMod& mod, const CheckContext& ctx) const {
    std::vector<QList<ModIssue>> per_checker;
    per_checker.reserve(checkers_.size());
    for (const auto& checker : checkers_) {
        per_checker.push_back(run_checker(*checker, mod, ctx));
    }
    return compose(per_checker);
}

QList<ModIssue> ModChecker::run_checker(const ModIssueChecker& checker,
                                        const AboutXmlMod& mod,
                                        const CheckContext& ctx) const {
    if (!checker.should_check(mod, ctx)) {
        return {};
    }
    // One checker must not abort diagnostics for every other mod and checker.
    try {
        return checker.check(mod, ctx);
    } catch (const std::exception& error) {
        spdlog::error("Checker failed on {}: {}",
                      mod.package_id.str().toStdString(),
                      error.what());
        return {};
    }
}

void ModChecker::emit_diagnostics() const {
    if (!on_diagnostics_changed_) {
        return;
    }
    DiagnosticsByUuid by_uuid;
    for (auto it = uuid_to_pid_.cbegin(); it != uuid_to_pid_.cend(); ++it) {
        const auto diagnostics = diagnostics_.constFind(it.value());
        if (diagnostics != diagnostics_.cend() &&
            (diagnostics->has_errors() || diagnostics->has_warnings())) {
            by_uuid.insert(it.key(), diagnostics.value());
        }
    }
    on_diagnostics_changed_(by_uuid);
}

}  // namespace px::modrim::checker
