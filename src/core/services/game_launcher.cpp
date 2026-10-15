#include "core/services/game_launcher.h"

#include <memory>
#include <utility>

#include <QCoro/QCoroFuture>
#include <QProcess>
#include <QProcessEnvironment>
#include <spdlog/spdlog.h>

#include "core/context.h"
#include "core/providers/base.h"

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace px::core::services {

namespace {

Result<QStringList> direct_argv(const QString& exe, const AppConfig& cfg, QHash<QString, QString>& env) {
    const QString args = QString::fromStdString(cfg.launch_args);
    if (current_platform() == Platform::MacOS) {
        auto split = split_arguments(args);
        if (!split) {
            return std::unexpected(split.error());
        }
        return QStringList{QStringLiteral("open"), exe, QStringLiteral("--args")} + *split;
    }
    auto command = build_direct_command(exe, QString::fromStdString(cfg.launch_wrapper), args);
    if (!command) {
        return std::unexpected(command.error());
    }
    env = std::move(command->env);
    return std::move(command->argv);
}

}  // namespace

GameLauncher::GameLauncher(CoreContext& ctx, ProcessLister lister, MonitorTiming timing)
    : ctx_(ctx), lister_(lister), monitor_(std::move(lister), timing) {}

QCoro::Task<Result<QString>> GameLauncher::launch(LaunchStrategy strategy) {
    if (ctx_.config().paths.game.empty()) {
        spdlog::warn("Launch aborted — game path not configured");
        co_return std::unexpected(Error{QStringLiteral("Game path not configured")});
    }

    spdlog::info("Launching game with strategy: {}",
                 strategy == LaunchStrategy::Direct ? "Direct" : "Steam");
    co_await monitor_.capture_baseline();
    if (strategy == LaunchStrategy::Direct) {
        co_return co_await launch_direct();
    }
    co_return co_await launch_steam();
}

bool GameLauncher::is_running() const {
    return is_game_running(lister_);
}

QCoro::Task<bool> GameLauncher::wait_for_start(std::chrono::milliseconds timeout) {
    return monitor_.wait_for_start(timeout);
}

QCoro::Task<std::optional<int>> GameLauncher::wait_for_exit() {
    return monitor_.wait_for_exit();
}

QCoro::Task<Result<QString>> GameLauncher::launch_direct() {
    const QString game = QString::fromStdString(ctx_.config().paths.game);
    const auto exe = find_executable(game);
    if (!exe) {
        spdlog::warn("Direct launch failed — executable not found in {}", game.toStdString());
        co_return std::unexpected(Error{QStringLiteral("Game executable not found")});
    }

    ensure_steam_appid(game);

    QHash<QString, QString> env;
    auto argv = direct_argv(*exe, ctx_.config(), env);
    if (!argv) {
        spdlog::warn("Invalid launch arguments: {}", argv.error().message.toStdString());
        co_return std::unexpected(
            Error{QStringLiteral("Invalid launch arguments: %1").arg(argv.error().message)});
    }

    spdlog::info("Spawning process: {} (cwd: {})", argv->join(QLatin1Char(' ')).toStdString(),
                 game.toStdString());
    auto proc = std::make_unique<QProcess>();
    proc->setProgram(argv->front());
    proc->setArguments(argv->mid(1));
    proc->setWorkingDirectory(game);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = env.cbegin(); it != env.cend(); ++it) {
        environment.insert(it.key(), it.value());
    }
    proc->setProcessEnvironment(environment);
    proc->setStandardOutputFile(QProcess::nullDevice());
    proc->setStandardErrorFile(QProcess::nullDevice());
#if defined(Q_OS_UNIX)
    // The game gets its own session, as it did under start_new_session=True.
    proc->setChildProcessModifier([] { ::setsid(); });
#endif
    proc->start();
    if (!proc->waitForStarted()) {
        const QString reason = proc->errorString();
        spdlog::warn("Direct launch failed: {}", reason.toStdString());
        co_return std::unexpected(Error{QStringLiteral("Failed to launch: %1").arg(reason)});
    }

    monitor_.attach_process(std::move(proc));
    co_return QStringLiteral("Game launched");
}

QCoro::Task<Result<QString>> GameLauncher::launch_steam() {
    const QString args = QString::fromStdString(ctx_.config().launch_args);
    auto opened = co_await providers::runOnPool<Result<void>>(nullptr, [args] {
        return open_steam_url(args);
    });
    if (!opened) {
        spdlog::warn("Steam launch failed: {}", opened.error().message.toStdString());
        co_return std::unexpected(
            Error{QStringLiteral("Steam launch failed: %1").arg(opened.error().message)});
    }
    co_return QStringLiteral("Launching via Steam...");
}

}  // namespace px::core::services
