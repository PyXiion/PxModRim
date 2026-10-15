#include "core/context.h"

#include <chrono>
#include <utility>

#include <QSet>
#include <spdlog/spdlog.h>

#include "core/downloads/downloader.h"
#include "core/downloads/manager.h"
#include "core/mod_service.h"
#include "core/profiler.h"
#include "core/providers/factory.h"
#include "core/services/activation_service.h"
#include "core/services/diagnostics_service.h"
#include "core/services/game_launcher.h"
#include "core/services/sort_service.h"

namespace px::core {

namespace {

// Minimum valid version string shape: "X.Y" — reject anything shorter.
constexpr qsizetype kVersionMinParts = 2;

}  // namespace

CoreContext::CoreContext(AppConfig cfg, ConfigService* config_service)
    : cfg_(std::move(cfg)), config_service_(config_service) {
    refresh_game_version();
    plugins_.register_plugin(std::make_unique<DownloadManager>());
}

CoreContext::~CoreContext() = default;

std::unique_ptr<CoreContext> CoreContext::create(AppConfig cfg, ConfigService* config_service) {
    std::unique_ptr<CoreContext> ctx(new CoreContext(std::move(cfg), config_service));
    ProfileSession session(QStringLiteral("services.create"));
    Profiler& timer = session.timer();
    {
        auto scope = timer(QStringLiteral("create_providers"));
        ctx->mod_service_ = std::make_unique<ModService>(
            *ctx, providers::create_providers(ctx->cfg_.paths, &ctx->pool_));
    }
    {
        auto scope = timer(QStringLiteral("diagnostics_service"));
        ctx->diagnostics_service_ = std::make_unique<DiagnosticsService>(*ctx, ctx->network_);
    }
    {
        auto scope = timer(QStringLiteral("sort_service"));
        ctx->sort_service_ = std::make_unique<SortService>(*ctx, *ctx->diagnostics_service_);
    }
    {
        auto scope = timer(QStringLiteral("game_launcher"));
        ctx->game_launcher_ = std::make_unique<services::GameLauncher>(*ctx);
    }
    {
        auto scope = timer(QStringLiteral("activation"));
        ctx->activation_ = std::make_unique<ActivationService>(*ctx);
    }
    return ctx;
}

void CoreContext::load(QHash<QString, ListedModPtr> mods, QStringList active_uuids) {
    mods_ = std::move(mods);
    loaded_active_uuids_ = active_uuids;
    active_uuids_ = std::move(active_uuids);
}

void CoreContext::setActive(QStringList uuids) {
    const QSet<QString> old_set(active_uuids_.cbegin(), active_uuids_.cend());
    const QSet<QString> new_set(uuids.cbegin(), uuids.cend());
    if (old_set == new_set && active_uuids_ == uuids) {
        return;
    }
    const bool set_changed = old_set != new_set;
    active_uuids_ = std::move(uuids);
    if (set_changed) {
        diagnostics_service_->rebuild(active_uuids_);
    } else {
        diagnostics_service_->reorder(active_uuids_);
    }
    active_state_changed_.notify(active_uuids_);
}

QCoro::Task<std::pair<int, double>> CoreContext::autoSort() {
    const QSet<QString> active(active_uuids_.cbegin(), active_uuids_.cend());
    const QStringList deps = sort_service_->resolve_missing_dependencies(active);
    if (!deps.isEmpty()) {
        active_uuids_.append(deps);
        diagnostics_service_->rebuild(active_uuids_);
        active_state_changed_.notify(active_uuids_);
        spdlog::info("auto-sort: enabling {} missing dependencies", deps.size());
    }

    const auto started = std::chrono::steady_clock::now();
    active_uuids_ = co_await sort_service_->sort_active_mods();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    diagnostics_service_->reorder(active_uuids_);
    active_state_changed_.notify(active_uuids_);
    const int count = static_cast<int>(active_uuids_.size());
    spdlog::info("auto-sort: {} mods sorted in {:.0f}ms", count, elapsed_ms);
    co_return std::pair{count, elapsed_ms};
}

void CoreContext::updateConfig(AppConfig cfg) {
    cfg_ = std::move(cfg);
    refresh_game_version();
    config_changed_.notify(cfg_);
}

void CoreContext::resetProviders(const PathConfig& paths) {
    mod_service_->reset_providers(providers::create_providers(paths, &pool_));
}

void CoreContext::refresh_game_version() {
    if (cfg_.paths.game.empty()) {
        game_version_ = QStringLiteral("Unknown");
        return;
    }
    const auto version = read_game_version(QString::fromStdString(cfg_.paths.game));
    game_version_ = version && !version->isEmpty() ? *version : QStringLiteral("Unknown");
}

QString CoreContext::targetVersion() const {
    const QStringList parts = game_version_.split(QLatin1Char('.'));
    if (parts.size() >= kVersionMinParts) {
        return QStringLiteral("%1.%2").arg(parts[0], parts[1]);
    }
    return QStringLiteral("Unknown");
}

CollectionStats CoreContext::computeStats(const std::optional<QStringList>& active_ids) const {
    const QStringList& ids = active_ids ? *active_ids : active_uuids_;
    CollectionStats stats;
    stats.total = mods_.size();
    stats.active = ids.size();
    stats.inactive = stats.total - stats.active;
    for (const auto& mod : mods_) {
        if (!mod->valid) {
            ++stats.errors;
        }
    }
    return stats;
}

ConfigService& CoreContext::configService() const {
    Q_ASSERT(config_service_ != nullptr);
    return *config_service_;
}

DownloadManager& CoreContext::downloads() const {
    auto* manager = dynamic_cast<DownloadManager*>(plugins_.get(kDownloadsManagerName));
    Q_ASSERT(manager != nullptr);
    return *manager;
}

QCoro::Task<Result<void>> CoreContext::initialize() {
    co_return co_await mod_service_->initialize();
}

QCoro::Task<void> CoreContext::close() {
    pool_.waitForDone();
    co_await mod_service_->startup_impact().close_connection();
    co_await mod_service_->close();
}

void CoreContext::closeSync() {
    pool_.waitForDone();
    mod_service_->startup_impact().close_connection_sync();
    mod_service_->close_sync();
}

void CoreContext::registerPlugin(std::unique_ptr<Plugin> plugin) {
    plugins_.register_plugin(std::move(plugin));
}

}  // namespace px::core
