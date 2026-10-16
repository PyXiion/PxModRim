#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <QCoro/QCoroTask>

#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "core/constants.hpp"
#include "core/context.hpp"
#include "core/services/game_launcher.hpp"
#include "core/services/game_process.hpp"
#include "services_support.hpp"

using px::modrim::AppConfig;
using px::modrim::CoreContext;
using px::modrim::LaunchStrategy;
using px::modrim::PathConfig;
using px::modrim::Result;
using px::modrim::services::buildDirectCommand;
using px::modrim::services::findExecutable;
using px::modrim::services::GameLauncher;
using px::modrim::services::GameProcessMonitor;
using px::modrim::services::isGameRunning;
using px::modrim::services::MonitorTiming;
using px::modrim::services::Platform;
using px::modrim::services::ProcessInfo;
using px::modrim::services::ProcessLister;
using px::modrim::services::splitArguments;
using px::modrim::services::steamRunUrl;
using services_test::EventLoop;
using services_test::runAsync;

namespace {

ProcessInfo proc(const QString& name, qint64 pid = 1, bool zombie = false) {
  return ProcessInfo{pid, name, zombie};
}

struct Replay {
  std::vector<std::vector<ProcessInfo>> snapshots;
  std::size_t next = 0;
};

// Lister that returns the snapshots in order, then keeps returning the last
// one.
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

const MonitorTiming kNoSleep{std::chrono::milliseconds(0),
                             std::chrono::milliseconds(0)};

QCoro::Task<bool> capture(GameProcessMonitor& monitor) {
  co_await monitor.captureBaseline();
  co_return true;
}

void touch(const QString& path) {
  QFile file(path);
  REQUIRE(file.open(QIODevice::WriteOnly));
}

void makeExecutable(const QString& path) {
  touch(path);
  REQUIRE(QFile::setPermissions(path, QFileDevice::ReadOwner |
                                          QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner));
}

std::unique_ptr<CoreContext> contextFor(const QString& gamePath) {
  static QTemporaryDir configDir;
  static px::modrim::ConfigService configService(configDir.path());
  AppConfig cfg;
  cfg.paths.game = gamePath.toStdString();
  return CoreContext::create(cfg, &configService);
}

}  // namespace

TEST_CASE(
    "command without wrapper is the executable followed by the arguments") {
  auto command = buildDirectCommand(QStringLiteral("/g/RimWorldLinux"), {},
                                    QStringLiteral("-popupwindow -logfile"),
                                    Platform::kLinux);
  REQUIRE(command.has_value());
  CHECK(command->argv ==
        QStringList{"/g/RimWorldLinux", "-popupwindow", "-logfile"});
  CHECK(command->env.isEmpty());
}

TEST_CASE("wrapper without placeholder is a prefix") {
  auto command =
      buildDirectCommand(QStringLiteral("/g/rw"), QStringLiteral("gamemoderun"),
                         QStringLiteral("-x"), Platform::kLinux);
  REQUIRE(command.has_value());
  CHECK(command->argv == QStringList{"gamemoderun", "/g/rw", "-x"});
}

TEST_CASE(
    "placeholder marks the game position and leading assignments become "
    "environment") {
  auto command = buildDirectCommand(
      QStringLiteral("/g/rw"),
      QStringLiteral("MANGOHUD=1 DXVK_HUD=fps mangohud %command% --tail"),
      QStringLiteral("-x"), Platform::kLinux);
  REQUIRE(command.has_value());
  CHECK(command->argv == QStringList{"mangohud", "/g/rw", "-x", "--tail"});
  CHECK(command->env.value(QStringLiteral("MANGOHUD")) == QStringLiteral("1"));
  CHECK(command->env.value(QStringLiteral("DXVK_HUD")) ==
        QStringLiteral("fps"));
}

TEST_CASE("quoted POSIX arguments stay one token") {
  auto command = buildDirectCommand(
      QStringLiteral("/g/rw"), {},
      QStringLiteral(R"(-savedatafolder="/tmp/my saves")"), Platform::kLinux);
  REQUIRE(command.has_value());
  CHECK(command->argv == QStringList{"/g/rw", "-savedatafolder=/tmp/my saves"});
}

TEST_CASE("an option containing '=' is not an environment assignment") {
  auto command =
      buildDirectCommand(QStringLiteral("/g/rw"),
                         QStringLiteral("--cfg=/p wrap"), {}, Platform::kLinux);
  REQUIRE(command.has_value());
  CHECK(command->argv == QStringList{"--cfg=/p", "wrap", "/g/rw"});
  CHECK(command->env.isEmpty());
}

TEST_CASE("unbalanced quotes are rejected") {
  CHECK_FALSE(buildDirectCommand(QStringLiteral("/g/rw"), {},
                                 QStringLiteral("\"oops"), Platform::kLinux)
                  .has_value());
}

TEST_CASE(
    "Windows splitting keeps quotes in the token, as shlex with posix=False "
    "does") {
  auto tokens =
      splitArguments(QStringLiteral(R"(-a "b c" d)"), Platform::kWindows);
  REQUIRE(tokens.has_value());
  CHECK(*tokens == QStringList{"-a", R"("b c")", "d"});
}

TEST_CASE("executable lookup prefers the current binary over the legacy one") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString root = dir.path();

  makeExecutable(QDir(root).filePath(QStringLiteral("RimWorldLinux")));
  makeExecutable(QDir(root).filePath(QStringLiteral("RimWorld")));
  auto found = findExecutable(root, Platform::kLinux);
  REQUIRE(found.has_value());
  CHECK(*found == QDir(root).filePath(QStringLiteral("RimWorld")));
}

TEST_CASE("executable lookup skips a non-executable binary") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString root = dir.path();

  touch(QDir(root).filePath(QStringLiteral("RimWorld")));
  makeExecutable(QDir(root).filePath(QStringLiteral("RimWorldLinux")));
  REQUIRE(
      QFile::setPermissions(QDir(root).filePath(QStringLiteral("RimWorld")),
                            QFileDevice::ReadOwner | QFileDevice::WriteOwner));

  auto found = findExecutable(root, Platform::kLinux);
  REQUIRE(found.has_value());
  CHECK(*found == QDir(root).filePath(QStringLiteral("RimWorldLinux")));
}

TEST_CASE("executable lookup finds the Windows and macOS layouts") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString root = dir.path();

  touch(QDir(root).filePath(QStringLiteral("RimWorldWin64.exe")));
  auto windows = findExecutable(root, Platform::kWindows);
  REQUIRE(windows.has_value());
  CHECK(*windows == QDir(root).filePath(QStringLiteral("RimWorldWin64.exe")));

  REQUIRE(QDir(root).mkpath(QStringLiteral("RimWorldMac.app")));
  auto mac = findExecutable(root, Platform::kMacOs);
  REQUIRE(mac.has_value());
  CHECK(*mac == QDir(root).filePath(QStringLiteral("RimWorldMac.app")));
}

TEST_CASE("steam URL is the app id alone without launch arguments") {
  CHECK(steamRunUrl(QString()) == QStringLiteral("steam://rungameid/294100"));
  CHECK(steamRunUrl(QStringLiteral("  ")) ==
        QStringLiteral("steam://rungameid/294100"));
}

TEST_CASE("steam URL percent-encodes launch arguments") {
  CHECK(steamRunUrl(QStringLiteral("-savedatafolder=/a b")) ==
        QStringLiteral("steam://run/294100//-savedatafolder%3D%2Fa%20b/"));
}

TEST_CASE("legacy and current game process names are recognized") {
  for (const char* name :
       {"RimWorld", "RimWorld.exe", "RimWorldLinux", "RimWorldWin64.exe",
        "RimWorldWin.exe", "RimWorld by Ludeon Studios"}) {
    const auto lister =
        replay({{proc(QString::fromLatin1(name)), proc("bash", 2)}});
    CHECK(isGameRunning(lister));
  }
}

TEST_CASE("zombie game process is not running") {
  const auto lister =
      replay({{proc("RimWorldLinux", 1, true), proc("bash", 2)}});
  CHECK_FALSE(isGameRunning(lister));
}

TEST_CASE("wait_for_exit returns once the game processes disappear") {
  EventLoop loop;
  // The first snapshot is the baseline, which is empty; then the game runs,
  // then exits.
  GameProcessMonitor monitor(
      replay({{}, {proc("RimWorldLinux")}, {proc("RimWorldLinux")}, {}}),
      kNoSleep);
  REQUIRE(runAsync(capture(monitor)));
  CHECK(runAsync(monitor.waitForExit()) == std::nullopt);
}

TEST_CASE("processes running before the launch are not counted as started") {
  EventLoop loop;
  // Baseline holds PID 10; the first poll still sees only PID 10, the second
  // sees PID 20 too.
  GameProcessMonitor monitor(
      replay({{proc("RimWorldLinux", 10)},
              {proc("RimWorldLinux", 10)},
              {proc("RimWorldLinux", 10), proc("RimWorldLinux", 20)}}),
      kNoSleep);
  REQUIRE(runAsync(capture(monitor)));
  CHECK(runAsync(monitor.waitForStart(std::chrono::milliseconds(5000))));
}

TEST_CASE(
    "wait_for_start is false when the spawned process died before the game "
    "appeared") {
  EventLoop loop;
  GameProcessMonitor monitor(replay({{}}), kNoSleep);
  monitor.attachProcess(
      std::make_unique<QProcess>());  // never started: state NotRunning
  CHECK_FALSE(runAsync(monitor.waitForStart(std::chrono::milliseconds(5000))));
}

#if defined(Q_OS_UNIX)
TEST_CASE("wait_for_exit reports the spawned process's exit code") {
  EventLoop loop;
  auto spawned = std::make_unique<QProcess>();
  spawned->start(QStringLiteral("/bin/sh"),
                 {QStringLiteral("-c"), QStringLiteral("exit 7")});
  REQUIRE(spawned->waitForFinished(10'000));

  GameProcessMonitor monitor(replay({{}}), kNoSleep);
  monitor.attachProcess(std::move(spawned));
  CHECK(runAsync(monitor.waitForExit()) == std::optional<int>(7));
}
#endif

TEST_CASE("launch without a game path fails before anything runs") {
  EventLoop loop;
  auto ctx = contextFor({});
  GameLauncher launcher(*ctx);
  const Result<QString> result =
      runAsync(launcher.launch(LaunchStrategy::kDirect));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().message == QStringLiteral("Game path not configured"));
}

TEST_CASE("direct launch without an executable fails") {
  EventLoop loop;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  auto ctx = contextFor(dir.path());
  GameLauncher launcher(*ctx);
  const Result<QString> result =
      runAsync(launcher.launch(LaunchStrategy::kDirect));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().message == QStringLiteral("Game executable not found"));
}
