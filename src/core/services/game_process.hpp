#pragma once

#include <QHash>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QCoro/QCoroTask>

#include "core/error.hpp"

namespace px::modrim::services {

enum class Platform {
  kLinux,
  kWindows,
  kMacOs,
};

// Returns the host's platform.
Platform currentPlatform();

// Splits a command line the way the platform's launcher does: POSIX shell
// quoting, or the Windows command-line rules. Fails on unbalanced quotes.
Result<QStringList> splitArguments(const QString& text,
                                   Platform platform = currentPlatform());

struct DirectCommand {
  QStringList argv;
  QHash<QString, QString> env;
};

// Expands wrapper and arguments into argv plus extra environment variables.
// Leading KEY=VALUE tokens in the wrapper set environment variables. %command%
// marks where the game goes; without it the wrapper is a prefix. Fails on
// unbalanced quotes.
Result<DirectCommand> buildDirectCommand(const QString& exe,
                                         const QString& wrapper,
                                         const QString& args,
                                         Platform platform = currentPlatform());

// Locates the RimWorld executable inside `game_path`, preferring modern Waylith
// binaries over leftover Unity files on disk.
std::optional<QString> findExecutable(const QString& gamePath,
                                      Platform platform = currentPlatform());

// Writes steam_appid.txt containing the RimWorld Steam app id into the game
// folder, unless it already exists. On macOS the file lives in the game
// directory's parent.
void ensureSteamAppid(const QString& gamePath);

// The steam:// URL that launches RimWorld with the given launch arguments.
QString steamRunUrl(const QString& launchArgs);

// Hands the steam:// URL to the desktop's handler. Fails when no opener could
// be started.
Result<void> openSteamUrl(const QString& launchArgs);

// A running process seen on the host.
struct ProcessInfo {
  qint64 pid = 0;
  QString name;
  bool zombie = false;
};

// Enumerates processes; an injectable double replaces this in tests.
using ProcessLister = std::function<std::vector<ProcessInfo>()>;

// Reads /proc on Linux, Toolhelp32Snapshot on Windows, libproc on macOS.
std::vector<ProcessInfo> hostProcessLister();

// Returns true when a non-zombie RimWorld game process is currently running.
bool isGameRunning(const ProcessLister& lister = hostProcessLister);

struct MonitorTiming {
  std::chrono::milliseconds pollStart{500};
  std::chrono::milliseconds pollExit{2000};
};

// Tracks the game process lifecycle: detects when a newly spawned game process
// starts, and when all game processes have finished. Process listing runs on
// the thread pool, so awaiting the methods never blocks the event loop.
class GameProcessMonitor {
 public:
  explicit GameProcessMonitor(ProcessLister lister = hostProcessLister,
                              MonitorTiming timing = {});
  ~GameProcessMonitor();

  GameProcessMonitor(const GameProcessMonitor&) = delete;
  GameProcessMonitor& operator=(const GameProcessMonitor&) = delete;

  // Records the game PIDs already running, so they are not counted as newly
  // started.
  QCoro::Task<void> captureBaseline();

  // Takes ownership of the spawned process; it must already be started.
  void attachProcess(std::unique_ptr<QProcess> proc);

  // Waits until a non-baseline game process appears. Returns false if the
  // timeout expires or the spawned process dies before a game process appears.
  QCoro::Task<bool> waitForStart(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(60'000));

  // Waits until every non-baseline game process exits. Returns the spawned
  // process's exit code if one was attached and has exited, or nullopt
  // otherwise.
  QCoro::Task<std::optional<int>> waitForExit();

 private:
  bool launchedAlive() const;
  // Forgets the spawned process. A child still running is released rather than
  // destroyed, because destroying a QProcess kills its child and the game must
  // outlive the manager.
  void dropProcess();

  ProcessLister m_lister;
  MonitorTiming m_timing;
  QSet<qint64> m_baseline;
  std::unique_ptr<QProcess> m_proc;
};

}  // namespace px::modrim::services
