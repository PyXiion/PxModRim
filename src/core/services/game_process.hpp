#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/error.hpp"

namespace px::modrim::services {

enum class Platform {
    Linux,
    Windows,
    MacOS,
};

// Returns the host's platform.
Platform current_platform();

// Splits a command line the way the platform's launcher does: POSIX shell quoting, or the
// Windows command-line rules. Fails on unbalanced quotes.
Result<QStringList> split_arguments(const QString& text, Platform platform = current_platform());

struct DirectCommand {
    QStringList argv;
    QHash<QString, QString> env;
};

// Expands wrapper and arguments into argv plus extra environment variables. Leading KEY=VALUE
// tokens in the wrapper set environment variables. %command% marks where the game goes;
// without it the wrapper is a prefix. Fails on unbalanced quotes.
Result<DirectCommand> build_direct_command(const QString& exe, const QString& wrapper,
                                           const QString& args,
                                           Platform platform = current_platform());

// Locates the RimWorld executable inside `game_path`, preferring modern Waylith binaries over
// leftover Unity files on disk.
std::optional<QString> find_executable(const QString& game_path,
                                       Platform platform = current_platform());

// Writes steam_appid.txt containing the RimWorld Steam app id into the game folder, unless it
// already exists. On macOS the file lives in the game directory's parent.
void ensure_steam_appid(const QString& game_path, Platform platform = current_platform());

// The steam:// URL that launches RimWorld with the given launch arguments.
QString steam_run_url(const QString& launch_args);

// Hands the steam:// URL to the desktop's handler. Fails when no opener could be started.
Result<void> open_steam_url(const QString& launch_args);

// A running process seen on the host.
struct ProcessInfo {
    qint64 pid = 0;
    QString name;
    bool zombie = false;
};

// Enumerates processes; an injectable double replaces this in tests.
using ProcessLister = std::function<std::vector<ProcessInfo>()>;

// Reads /proc on Linux, Toolhelp32Snapshot on Windows, libproc on macOS.
std::vector<ProcessInfo> host_process_lister();

// Returns true when a non-zombie RimWorld game process is currently running.
bool is_game_running(const ProcessLister& lister = host_process_lister);

// Returns the set of non-zombie RimWorld PIDs found by `lister`.
QSet<qint64> live_game_pids(const ProcessLister& lister = host_process_lister);

struct MonitorTiming {
    std::chrono::milliseconds poll_start{500};
    std::chrono::milliseconds poll_exit{2000};
};

// Tracks the game process lifecycle: detects when a newly spawned game process starts, and when
// all game processes have finished. Process listing runs on the thread pool, so awaiting the
// methods never blocks the event loop.
class GameProcessMonitor {
public:
    explicit GameProcessMonitor(ProcessLister lister = host_process_lister,
                                MonitorTiming timing = {});
    ~GameProcessMonitor();

    GameProcessMonitor(const GameProcessMonitor&) = delete;
    GameProcessMonitor& operator=(const GameProcessMonitor&) = delete;

    // Records the game PIDs already running, so they are not counted as newly started.
    QCoro::Task<void> capture_baseline();

    // Takes ownership of the spawned process; it must already be started.
    void attach_process(std::unique_ptr<QProcess> proc);

    // Waits until a non-baseline game process appears. Returns false if the timeout expires or
    // the spawned process dies before a game process appears.
    QCoro::Task<bool> wait_for_start(std::chrono::milliseconds timeout = std::chrono::milliseconds(60'000));

    // Waits until every non-baseline game process exits. Returns the spawned process's exit code
    // if one was attached and has exited, or nullopt otherwise.
    QCoro::Task<std::optional<int>> wait_for_exit();

private:
    bool launched_alive() const;
    // Forgets the spawned process. A child still running is released rather than destroyed,
    // because destroying a QProcess kills its child and the game must outlive the manager.
    void drop_process();

    ProcessLister lister_;
    MonitorTiming timing_;
    QSet<qint64> baseline_;
    std::unique_ptr<QProcess> proc_;
};

}  // namespace px::modrim::services
