#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <QCoro/QCoroTask>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QProcess>
#include <QTemporaryDir>
#include <QString>
#include <QStringList>

#include "core/config.h"
#include "core/constants.h"
#include "core/context.h"
#include "core/services/game_launcher.h"
#include "core/services/game_process.h"
#include "services_support.h"

using px::core::AppConfig;
using px::core::CoreContext;
using px::core::LaunchStrategy;
using px::core::PathConfig;
using px::core::Result;
using px::core::services::build_direct_command;
using px::core::services::find_executable;
using px::core::services::GameLauncher;
using px::core::services::GameProcessMonitor;
using px::core::services::is_game_running;
using px::core::services::MonitorTiming;
using px::core::services::Platform;
using px::core::services::ProcessInfo;
using px::core::services::ProcessLister;
using px::core::services::split_arguments;
using px::core::services::steam_run_url;
using services_test::EventLoop;
using services_test::run_async;

namespace {

ProcessInfo proc(const QString& name, qint64 pid = 1, bool zombie = false) {
    return ProcessInfo{pid, name, zombie};
}

struct Replay {
    std::vector<std::vector<ProcessInfo>> snapshots;
    std::size_t next = 0;
};

// Lister that returns the snapshots in order, then keeps returning the last one.
ProcessLister replay(std::vector<std::vector<ProcessInfo>> snapshots) {
    auto state = std::make_shared<Replay>(Replay{std::move(snapshots), 0});
    return [state] {
        const std::size_t last = state->snapshots.size() - 1;
        const auto snapshot = state->snapshots[std::min(state->next, last)];
        if (state->next < last) {
            ++state->next;
        }
        return snapshot;
    };
}

const MonitorTiming kNoSleep{std::chrono::milliseconds(0), std::chrono::milliseconds(0)};

QCoro::Task<bool> capture(GameProcessMonitor& monitor) {
    co_await monitor.capture_baseline();
    co_return true;
}

void touch(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
}

void make_executable(const QString& path) {
    touch(path);
    REQUIRE(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                            QFileDevice::ExeOwner));
}

std::unique_ptr<CoreContext> context_for(const QString& game_path) {
    static QTemporaryDir configDir;
    static px::core::ConfigService configService(configDir.path());
    AppConfig cfg;
    cfg.paths.game = game_path.toStdString();
    return CoreContext::create(cfg, &configService);
}

}  // namespace

TEST_CASE("command without wrapper is the executable followed by the arguments") {
    auto command = build_direct_command(QStringLiteral("/g/RimWorldLinux"), {},
                                        QStringLiteral("-popupwindow -logfile"), Platform::Linux);
    REQUIRE(command.has_value());
    CHECK(command->argv == QStringList{"/g/RimWorldLinux", "-popupwindow", "-logfile"});
    CHECK(command->env.isEmpty());
}

TEST_CASE("wrapper without placeholder is a prefix") {
    auto command = build_direct_command(QStringLiteral("/g/rw"), QStringLiteral("gamemoderun"),
                                        QStringLiteral("-x"), Platform::Linux);
    REQUIRE(command.has_value());
    CHECK(command->argv == QStringList{"gamemoderun", "/g/rw", "-x"});
}

TEST_CASE("placeholder marks the game position and leading assignments become environment") {
    auto command = build_direct_command(
        QStringLiteral("/g/rw"), QStringLiteral("MANGOHUD=1 DXVK_HUD=fps mangohud %command% --tail"),
        QStringLiteral("-x"), Platform::Linux);
    REQUIRE(command.has_value());
    CHECK(command->argv == QStringList{"mangohud", "/g/rw", "-x", "--tail"});
    CHECK(command->env.value(QStringLiteral("MANGOHUD")) == QStringLiteral("1"));
    CHECK(command->env.value(QStringLiteral("DXVK_HUD")) == QStringLiteral("fps"));
}

TEST_CASE("quoted POSIX arguments stay one token") {
    auto command = build_direct_command(QStringLiteral("/g/rw"), {},
                                        QStringLiteral(R"(-savedatafolder="/tmp/my saves")"),
                                        Platform::Linux);
    REQUIRE(command.has_value());
    CHECK(command->argv == QStringList{"/g/rw", "-savedatafolder=/tmp/my saves"});
}

TEST_CASE("an option containing '=' is not an environment assignment") {
    auto command = build_direct_command(QStringLiteral("/g/rw"), QStringLiteral("--cfg=/p wrap"), {},
                                        Platform::Linux);
    REQUIRE(command.has_value());
    CHECK(command->argv == QStringList{"--cfg=/p", "wrap", "/g/rw"});
    CHECK(command->env.isEmpty());
}

TEST_CASE("unbalanced quotes are rejected") {
    CHECK_FALSE(build_direct_command(QStringLiteral("/g/rw"), {}, QStringLiteral("\"oops"),
                                     Platform::Linux)
                    .has_value());
}

TEST_CASE("Windows splitting keeps quotes in the token, as shlex with posix=False does") {
    auto tokens = split_arguments(QStringLiteral(R"(-a "b c" d)"), Platform::Windows);
    REQUIRE(tokens.has_value());
    CHECK(*tokens == QStringList{"-a", R"("b c")", "d"});
}

TEST_CASE("executable lookup prefers the current binary over the legacy one") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString root = dir.path();

    make_executable(QDir(root).filePath(QStringLiteral("RimWorldLinux")));
    make_executable(QDir(root).filePath(QStringLiteral("RimWorld")));
    auto found = find_executable(root, Platform::Linux);
    REQUIRE(found.has_value());
    CHECK(*found == QDir(root).filePath(QStringLiteral("RimWorld")));
}

TEST_CASE("executable lookup skips a non-executable binary") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString root = dir.path();

    touch(QDir(root).filePath(QStringLiteral("RimWorld")));
    make_executable(QDir(root).filePath(QStringLiteral("RimWorldLinux")));
    REQUIRE(QFile::setPermissions(QDir(root).filePath(QStringLiteral("RimWorld")),
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    auto found = find_executable(root, Platform::Linux);
    REQUIRE(found.has_value());
    CHECK(*found == QDir(root).filePath(QStringLiteral("RimWorldLinux")));
}

TEST_CASE("executable lookup finds the Windows and macOS layouts") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString root = dir.path();

    touch(QDir(root).filePath(QStringLiteral("RimWorldWin64.exe")));
    auto windows = find_executable(root, Platform::Windows);
    REQUIRE(windows.has_value());
    CHECK(*windows == QDir(root).filePath(QStringLiteral("RimWorldWin64.exe")));

    REQUIRE(QDir(root).mkpath(QStringLiteral("RimWorldMac.app")));
    auto mac = find_executable(root, Platform::MacOS);
    REQUIRE(mac.has_value());
    CHECK(*mac == QDir(root).filePath(QStringLiteral("RimWorldMac.app")));
}

TEST_CASE("steam URL is the app id alone without launch arguments") {
    CHECK(steam_run_url(QString()) == QStringLiteral("steam://rungameid/294100"));
    CHECK(steam_run_url(QStringLiteral("  ")) == QStringLiteral("steam://rungameid/294100"));
}

TEST_CASE("steam URL percent-encodes launch arguments") {
    CHECK(steam_run_url(QStringLiteral("-savedatafolder=/a b")) ==
          QStringLiteral("steam://run/294100//-savedatafolder%3D%2Fa%20b/"));
}

TEST_CASE("legacy and current game process names are recognized") {
    for (const char* name : {"RimWorld", "RimWorld.exe", "RimWorldLinux", "RimWorldWin64.exe",
                             "RimWorldWin.exe", "RimWorld by Ludeon Studios"}) {
        const auto lister = replay({{proc(QString::fromLatin1(name)), proc("bash", 2)}});
        CHECK(is_game_running(lister));
    }
}

TEST_CASE("zombie game process is not running") {
    const auto lister = replay({{proc("RimWorldLinux", 1, true), proc("bash", 2)}});
    CHECK_FALSE(is_game_running(lister));
}

TEST_CASE("wait_for_exit returns once the game processes disappear") {
    EventLoop loop;
    // The first snapshot is the baseline, which is empty; then the game runs, then exits.
    GameProcessMonitor monitor(replay({{}, {proc("RimWorldLinux")}, {proc("RimWorldLinux")}, {}}),
                               kNoSleep);
    REQUIRE(run_async(capture(monitor)));
    CHECK(run_async(monitor.wait_for_exit()) == std::nullopt);
}

TEST_CASE("processes running before the launch are not counted as started") {
    EventLoop loop;
    // Baseline holds PID 10; the first poll still sees only PID 10, the second sees PID 20 too.
    GameProcessMonitor monitor(replay({{proc("RimWorldLinux", 10)},
                                       {proc("RimWorldLinux", 10)},
                                       {proc("RimWorldLinux", 10), proc("RimWorldLinux", 20)}}),
                               kNoSleep);
    REQUIRE(run_async(capture(monitor)));
    CHECK(run_async(monitor.wait_for_start(std::chrono::milliseconds(5000))));
}

TEST_CASE("wait_for_start is false when the spawned process died before the game appeared") {
    EventLoop loop;
    GameProcessMonitor monitor(replay({{}}), kNoSleep);
    monitor.attach_process(std::make_unique<QProcess>());  // never started: state NotRunning
    CHECK_FALSE(run_async(monitor.wait_for_start(std::chrono::milliseconds(5000))));
}

#if defined(Q_OS_UNIX)
TEST_CASE("wait_for_exit reports the spawned process's exit code") {
    EventLoop loop;
    auto spawned = std::make_unique<QProcess>();
    spawned->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("exit 7")});
    REQUIRE(spawned->waitForFinished(10'000));

    GameProcessMonitor monitor(replay({{}}), kNoSleep);
    monitor.attach_process(std::move(spawned));
    CHECK(run_async(monitor.wait_for_exit()) == std::optional<int>(7));
}
#endif

TEST_CASE("launch without a game path fails before anything runs") {
    EventLoop loop;
    auto ctx = context_for({});
    GameLauncher launcher(*ctx);
    const Result<QString> result = run_async(launcher.launch(LaunchStrategy::Direct));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == QStringLiteral("Game path not configured"));
}

TEST_CASE("direct launch without an executable fails") {
    EventLoop loop;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto ctx = context_for(dir.path());
    GameLauncher launcher(*ctx);
    const Result<QString> result = run_async(launcher.launch(LaunchStrategy::Direct));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == QStringLiteral("Game executable not found"));
}
