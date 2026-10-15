#pragma once

#include <chrono>
#include <optional>

#include <QCoro/QCoroTask>
#include <QString>

#include "core/constants.h"
#include "core/error.h"
#include "core/services/game_process.h"

namespace px::core {
class CoreContext;
}

namespace px::core::services {

// Starts RimWorld directly or through Steam, and tracks the game until it exits.
class GameLauncher {
public:
    // `ctx` is not owned and must outlive the launcher.
    GameLauncher(CoreContext& ctx, ProcessLister lister = host_process_lister,
                 MonitorTiming timing = {});

    // Resolves to a status message when the launch was started, or to the reason it was not.
    QCoro::Task<Result<QString>> launch(LaunchStrategy strategy);

    // Whether a game process is running right now. Blocks while listing processes.
    bool is_running() const;

    QCoro::Task<bool> wait_for_start(std::chrono::milliseconds timeout = std::chrono::milliseconds(60'000));

    // Resolves to the spawned process's exit code, or nullopt when it was not spawned here or
    // has not exited.
    QCoro::Task<std::optional<int>> wait_for_exit();

private:
    QCoro::Task<Result<QString>> launch_direct();
    QCoro::Task<Result<QString>> launch_steam();

    CoreContext& ctx_;
    ProcessLister lister_;
    GameProcessMonitor monitor_;
};

}  // namespace px::core::services
