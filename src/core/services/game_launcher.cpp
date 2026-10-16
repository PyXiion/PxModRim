#include "core/services/game_launcher.hpp"

#include <memory>
#include <utility>

#include <QCoro/QCoroFuture>
#include <QProcess>
#include <QProcessEnvironment>
#include <spdlog/spdlog.h>

#include "core/context.hpp"
#include "core/providers/base.hpp"

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace px::modrim::services {

namespace {

Result<QStringList> directArgv(const QString& exe, const AppConfig& cfg, QHash<QString, QString>& env) {
    const QString args = QString::fromStdString(cfg.launch_args);
    if (currentPlatform() == Platform::kMacOs) {
        auto split = splitArguments(args);
        if (!split) {
            return std::unexpected(split.error());
        }
        return QStringList{QStringLiteral("open"), exe, QStringLiteral("--args")} + *split;
    }
    auto command = buildDirectCommand(exe, QString::fromStdString(cfg.launch_wrapper), args);
    if (!command) {
        return std::unexpected(command.error());
    }
    env = std::move(command->env);
    return std::move(command->argv);
}

}  // namespace

GameLauncher::GameLauncher(CoreContext& ctx, ProcessLister lister, MonitorTiming timing)
    : m_ctx(ctx), m_lister(lister), m_monitor(std::move(lister), timing) {}

QCoro::Task<Result<QString>> GameLauncher::launch(LaunchStrategy strategy) {
    if (m_ctx.config().paths.game.empty()) {
        spdlog::warn("Launch aborted — game path not configured");
        co_return std::unexpected(Error{QStringLiteral("Game path not configured")});
    }

    spdlog::info("Launching game with strategy: {}",
                 strategy == LaunchStrategy::kDirect ? "Direct" : "Steam");
    co_await m_monitor.captureBaseline();
    if (strategy == LaunchStrategy::kDirect) {
        co_return co_await launchDirect();
    }
    co_return co_await launchSteam();
}

bool GameLauncher::isRunning() const {
    return isGameRunning(m_lister);
}

QCoro::Task<bool> GameLauncher::waitForStart(std::chrono::milliseconds timeout) {
    return m_monitor.waitForStart(timeout);
}

QCoro::Task<std::optional<int>> GameLauncher::waitForExit() {
    return m_monitor.waitForExit();
}

QCoro::Task<Result<QString>> GameLauncher::launchDirect() {
    const QString game = QString::fromStdString(m_ctx.config().paths.game);
    const auto exe = findExecutable(game);
    if (!exe) {
        spdlog::warn("Direct launch failed — executable not found in {}", game.toStdString());
        co_return std::unexpected(Error{QStringLiteral("Game executable not found")});
    }

    ensureSteamAppid(game);

    QHash<QString, QString> env;
    auto argv = directArgv(*exe, m_ctx.config(), env);
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

    m_monitor.attachProcess(std::move(proc));
    co_return QStringLiteral("Game launched");
}

QCoro::Task<Result<QString>> GameLauncher::launchSteam() {
    const QString args = QString::fromStdString(m_ctx.config().launch_args);
    auto opened = co_await providers::runOnPool<Result<void>>(nullptr, [args] {
        return openSteamUrl(args);
    });
    if (!opened) {
        spdlog::warn("Steam launch failed: {}", opened.error().message.toStdString());
        co_return std::unexpected(
            Error{QStringLiteral("Steam launch failed: %1").arg(opened.error().message)});
    }
    co_return QStringLiteral("Launching via Steam...");
}

}  // namespace px::modrim::services
