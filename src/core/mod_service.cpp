#include "core/mod_service.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <spdlog/spdlog.h>

#include "core/context.h"
#include "core/mods_config.h"
#include "core/services/diagnostics_service.h"
#include "core/services/mod_discovery.h"

namespace px::core {

namespace {

QString config_folder_of(const CoreContext& ctx) {
    return QString::fromStdString(ctx.config().paths.config_folder);
}

}  // namespace

ModService::ModService(CoreContext& ctx, providers::ProviderList providers,
                       std::unique_ptr<services::MetadataCache> metadata_cache)
    : ctx_(ctx),
      providers_(std::move(providers)),
      metadata_cache_(std::move(metadata_cache)),
      startup_impact_(ctx) {
    if (!metadata_cache_ && ctx.hasConfigService()) {
        metadata_cache_ = std::make_unique<services::MetadataCache>(
            services::metadata_cache_path(ctx.configService().directory()));
    }
}

void ModService::reset_providers(providers::ProviderList providers) {
    providers_ = std::move(providers);
}

providers::BaseModProvider* ModService::get_provider(const QString& provider_id) const {
    for (const auto& provider : providers_) {
        if (provider->provider_id() == provider_id) {
            return provider.get();
        }
    }
    return nullptr;
}

QHash<QString, QString> ModService::provider_colors() const {
    QHash<QString, QString> colors;
    for (const auto& provider : providers_) {
        colors.insert(provider->provider_id(), provider->color());
    }
    return colors;
}

QCoro::Task<Result<providers::ModMap>> ModService::discover_one(providers::BaseModProvider& provider,
                                                                Profiler& timer, bool force_reparse) {
    auto scope = timer(QStringLiteral("provider.%1").arg(provider.provider_id()));
    co_return co_await provider.discover(ctx_.targetVersion(), &timer, metadata_cache_.get(),
                                         force_reparse);
}

QCoro::Task<Result<void>> ModService::discover(Profiler* timer, bool force_reparse) {
    // Each provider profiles into its own tree, since their discoveries interleave.
    std::unique_ptr<Profiler> own_timer;
    if (!timer) {
        own_timer = std::make_unique<Profiler>();
        timer = own_timer.get();
    }
    auto scope = (*timer)(QStringLiteral("discover"));

    std::vector<std::unique_ptr<Profiler>> child_timers;
    std::vector<QCoro::Task<Result<providers::ModMap>>> tasks;
    child_timers.reserve(providers_.size());
    tasks.reserve(providers_.size());
    for (const auto& provider : providers_) {
        child_timers.push_back(std::make_unique<Profiler>());
        tasks.push_back(discover_one(*provider, *child_timers.back(), force_reparse));
    }

    providers::ModMap all_mods;
    std::optional<Error> failure;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        auto result = co_await tasks[i];
        timer->adopt(*child_timers[i]);
        if (!result) {
            if (!failure) {
                failure = result.error();
            }
            continue;
        }
        for (auto it = result->cbegin(); it != result->cend(); ++it) {
            all_mods.insert(it.key(), it.value());
        }
    }
    if (failure) {
        co_return std::unexpected(*failure);
    }

    if (metadata_cache_) {
        auto prune = (*timer)(QStringLiteral("cache_prune"));
        QStringList mod_paths;
        for (const auto& mod : all_mods) {
            if (mod->mod_path()) {
                mod_paths.append(*mod->mod_path());
            }
        }
        const QStringList live_paths = co_await providers::runOnPool<QStringList>(
            nullptr, [mod_paths] { return services::normalize_paths(mod_paths); });
        co_await metadata_cache_->prune_disappeared(QSet<QString>(live_paths.cbegin(), live_paths.cend()));
    }

    {
        auto resolve = (*timer)(QStringLiteral("resolve_active"));
        const QStringList active = services::resolve_active_uuids(all_mods, config_folder_of(ctx_));
        ctx_.load(std::move(all_mods), active);
    }

    if (own_timer) {
        spdlog::debug("Profile report:\n{}", own_timer->render().toStdString());
    }
    co_return {};
}

QCoro::Task<Result<void>> ModService::run_refresh_pipeline(const QString& label, bool force_reparse) {
    Profiler timer;
    auto scope = timer(QStringLiteral("mod_service.%1").arg(label));

    Profiler discover_timer;
    Profiler diagnostics_timer;
    Profiler impact_timer;
    auto discovery = discover(&discover_timer, force_reparse);
    auto diagnostics = ctx_.diagnosticsService().initialize(&diagnostics_timer);
    auto impact = startup_impact_.load();

    const auto discovered = co_await discovery;
    const auto initialized = co_await diagnostics;
    const auto loaded = co_await impact;
    timer.adopt(discover_timer);
    timer.adopt(diagnostics_timer);
    timer.adopt(impact_timer);

    if (!discovered) {
        co_return std::unexpected(discovered.error());
    }
    if (!initialized) {
        co_return std::unexpected(initialized.error());
    }
    if (!loaded) {
        co_return std::unexpected(loaded.error());
    }
    ctx_.diagnosticsService().rebuild(ctx_.activeUuids(), &timer);
    spdlog::debug("Profile report:\n{}", timer.render().toStdString());
    mods_changed.notify(std::monostate{});
    co_return {};
}

std::optional<QString> ModService::snapshots_dir() const {
    if (!ctx_.hasConfigService()) {
        return std::nullopt;
    }
    return QDir(ctx_.configService().directory()).filePath(QStringLiteral("snapshots"));
}

void ModService::snapshot_current_config() {
    const QString config_folder = config_folder_of(ctx_);
    const auto snapshots = snapshots_dir();
    if (config_folder.isEmpty() || !snapshots) {
        return;
    }
    const QString config_path = QDir(config_folder).filePath(QStringLiteral("ModsConfig.xml"));
    if (!QFileInfo(config_path).isFile()) {
        return;
    }
    if (auto created = create_snapshot(config_path, *snapshots, ctx_.config().max_snapshots); !created) {
        spdlog::warn("Failed to snapshot ModsConfig.xml: {}", created.error().message.toStdString());
    }
}

QCoro::Task<Result<void>> ModService::initialize() {
    snapshot_current_config();
    co_return co_await run_refresh_pipeline(QStringLiteral("initialize"), false);
}

QCoro::Task<Result<void>> ModService::reload(bool full) {
    co_return co_await run_refresh_pipeline(QStringLiteral("reload"), full);
}

QStringList ModService::get_snapshots() const {
    const auto snapshots = snapshots_dir();
    return snapshots ? px::core::get_snapshots(*snapshots) : QStringList{};
}

QCoro::Task<bool> ModService::restore_snapshot(QString snapshot_path) {
    const QString config_folder = config_folder_of(ctx_);
    const auto snapshots = snapshots_dir();
    if (config_folder.isEmpty() || !snapshots) {
        spdlog::warn("Cannot restore snapshot: configuration is incomplete");
        co_return false;
    }

    const QString config_path = QDir(config_folder).filePath(QStringLiteral("ModsConfig.xml"));
    auto restored = px::core::restore_snapshot(snapshot_path, config_path, *snapshots,
                                               ctx_.config().max_snapshots);
    if (!restored) {
        spdlog::warn("Failed to restore snapshot: {}", restored.error().message.toStdString());
        co_return false;
    }
    auto reloaded = co_await reload();
    if (!reloaded) {
        spdlog::error("Reload after snapshot restore failed: {}",
                      reloaded.error().message.toStdString());
        co_return false;
    }
    co_return true;
}

QCoro::Task<bool> ModService::save_active_layout(QStringList active_ids) {
    const QString config_folder = config_folder_of(ctx_);
    const auto snapshots = snapshots_dir();
    if (config_folder.isEmpty() || !snapshots) {
        co_return false;
    }

    const auto& mods = ctx_.allMods();
    QList<CaseInsensitiveStr> package_ids;
    for (const QString& uuid : active_ids) {
        if (auto about = std::dynamic_pointer_cast<AboutXmlMod>(mods.value(uuid))) {
            package_ids.append(about->package_id);
        }
    }
    const QString config_path = QDir(config_folder).filePath(QStringLiteral("ModsConfig.xml"));
    const QString target_version = ctx_.targetVersion();
    const int max_snapshots = ctx_.config().max_snapshots;
    const QString snapshots_path = *snapshots;

    auto written = co_await providers::runOnPool<Result<void>>(
        nullptr, [this, config_path, snapshots_path, target_version, max_snapshots,
                  package_ids = std::move(package_ids)] {
            QMutexLocker lock(&save_mutex_);
            const auto existing = parse_mods_config(config_path);
            ModsConfig data;
            data.version = target_version;
            data.activeMods = package_ids;
            data.knownExpansions = existing ? existing->knownExpansions : QList<CaseInsensitiveStr>{};
            return write_mods_config(config_path, data, snapshots_path, max_snapshots);
        });
    if (!written) {
        spdlog::warn("Failed to save ModsConfig.xml: {}", written.error().message.toStdString());
        co_return false;
    }
    co_return true;
}

QCoro::Task<void> ModService::close() {
    if (metadata_cache_) {
        co_await metadata_cache_->close();
    }
}

void ModService::close_sync() {
    // Destroying the cache closes its connection and joins its worker thread.
    metadata_cache_.reset();
}

}  // namespace px::core
