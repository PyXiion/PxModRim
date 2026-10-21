#pragma once

#include <QString>
#include <chrono>
#include <optional>

#include <QCoro/QCoroTask>

#include "core/constants.hpp"
#include "core/error.hpp"
#include "core/services/game_process.hpp"

namespace px::modrim {
class CoreContext;
}

namespace px::modrim::services {

// Starts RimWorld directly or through Steam, and tracks the game until it
// exits.
class GameLauncher {
 public:
  // `ctx` is not owned and must outlive the launcher.
  explicit GameLauncher(CoreContext& ctx);

  // Resolves to a status message when the launch was started, or to the reason
  // it was not.
  QCoro::Task<Result<QString>> launch(LaunchStrategy strategy);

  // Whether a game process is running right now. Blocks while listing
  // processes.
  bool isRunning() const;

  QCoro::Task<bool> waitForStart(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(60'000));

  // Resolves to the spawned process's exit code, or nullopt when it was not
  // spawned here or has not exited.
  QCoro::Task<std::optional<int>> waitForExit();

 private:
  QCoro::Task<Result<QString>> launchDirect();
  QCoro::Task<Result<QString>> launchSteam();

  CoreContext& m_ctx;
  GameProcessMonitor m_monitor;
};

}  // namespace px::modrim::services
