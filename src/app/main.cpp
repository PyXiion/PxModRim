#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStyleHints>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <QCoro/QCoroSignal>
#include <QCoro/QCoroTask>
#include <QtQml/QQmlExtensionPlugin>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "app/plugins.hpp"
#include "core/config.hpp"
#include "core/context.hpp"
#include "core/organizer/db.hpp"
#include "core/version.hpp"
#include "ui/components/popup_parent.hpp"
#include "ui/config.hpp"
#include "ui/context.hpp"
#include "ui/theme/palette.hpp"
#include "ui/window/main_window.hpp"
#include "ui/window/restart.hpp"

Q_IMPORT_QML_PLUGIN(PxModRimPlugin)

namespace {

constexpr int kWheelScrollLines = 8;
constexpr qint64 kLogMaxBytes = 10 * 1024 * 1024;
constexpr std::size_t kLogRetention = 5;

enum class AppExit { kSuccess, kFailure, kRestart };

// Qt6 defaults to PassThrough, which makes QtWebEngine render at integer buffer
// scale while the compositor upscales fractional scales, which blurs.
// RoundPreferFloor avoids the blur at the cost of slightly smaller UI (the
// QTBUG-113574 workaround).
void setEnvDefaults() {
  const std::pair<const char*, const char*> defaults[] = {
      {"QT_SCALE_FACTOR_ROUNDING_POLICY", "RoundPreferFloor"},
      {"QT_ENABLE_HIGHDPI_SCALING", "1"},
      {"QT_QUICK_CONTROLS_STYLE", "Basic"},
  };
  for (const auto& [name, value] : defaults) {
    if (!qEnvironmentVariableIsSet(name)) {
      qputenv(name, value);
    }
  }
}

// PX_LOG_LEVEL takes precedence over LOGURU_LEVEL (the legacy name). Unknown
// values and loguru's SUCCESS level fall back to info.
spdlog::level::level_enum logLevelFromEnv() {
  for (const char* name : {"PX_LOG_LEVEL", "LOGURU_LEVEL"}) {
    const std::string value =
        qEnvironmentVariable(name).trimmed().toLower().toStdString();
    if (value.empty()) {
      continue;
    }
    const spdlog::level::level_enum level = spdlog::level::from_str(value);
    if (level != spdlog::level::off || value == "off") {
      return level;
    }
  }
  return spdlog::level::info;
}

// Logs to stderr, and to a rotating file under the config directory when it can
// be opened.
void configureLogging(const QString& logsDir) {
  std::vector<spdlog::sink_ptr> sinks{
      std::make_shared<spdlog::sinks::stderr_color_sink_mt>()};
  auto logger =
      std::make_shared<spdlog::logger>("pxmodrim", sinks.begin(), sinks.end());
  logger->set_pattern("%Y-%m-%d %H:%M:%S.%e | %^%-8l%$ | %v");
  logger->set_level(logLevelFromEnv());
  spdlog::set_default_logger(logger);

  QDir().mkpath(logsDir);
  const std::string logFile =
      QDir(logsDir).filePath(QStringLiteral("pxmodrim.log")).toStdString();
  try {
    // spdlog reports an unopenable file by throwing; that is the only way it
    // reports it.
    logger->sinks().push_back(
        std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFile, kLogMaxBytes, kLogRetention));
  } catch (const spdlog::spdlog_ex& ex) {
    spdlog::warn("Logging to {} is disabled: {}", logFile, ex.what());
  }
}

QStringList commandLine(int argc, char* argv[]) {
  QStringList arguments;
  arguments.reserve(argc);
  for (int index = 0; index < argc; ++index) {
    arguments.append(QString::fromLocal8Bit(argv[index]));
  }
  return arguments;
}

// Exports the scale as QT_SCALE_FACTOR, so it must run before QApplication
// exists. A user-set QT_SCALE_FACTOR is left alone.
px::modrim::ResolvedScale applyStartupScale(const QString& cliScale,
                                            const QString& envScale) {
  const px::modrim::ConfigService config(px::modrim::configDir());
  const auto scale = px::modrim::resolveUiScale(
      px::modrim::loadUiScale(config), cliScale, envScale,
      qEnvironmentVariable("QT_SCALE_FACTOR"));
  if (scale.source != px::modrim::ScaleSource::kQtEnv && scale.scale != 100) {
    qputenv("QT_SCALE_FACTOR",
            px::modrim::uiScaleFactor(static_cast<int>(scale.scale)));
  }
  return scale;
}

// Runs once logging exists, which is after the scale had to be applied.
void warnInvalidScaleOverrides(const QString& cliScale,
                               const QString& envScale) {
  for (const auto& [source, value] : {std::pair{"--ui-scale", cliScale},
                                      std::pair{"PX_UI_SCALE", envScale}}) {
    if (!value.isNull() && !px::modrim::parseScaleOverride(value)) {
      spdlog::warn("Ignoring invalid {} value: {}", source,
                   value.toStdString());
    }
  }
}

// Must run after the previous run has shut its plugins down and destroyed its
// services, so the new process never shares configuration or databases.
bool relaunch(const QStringList& originalArguments, bool keepQtScale) {
  const auto environment = QProcessEnvironment::systemEnvironment();
  const QString program = px::modrim::restartProgram(
      environment, QCoreApplication::applicationFilePath(),
      [](const QString& path) { return QFileInfo(path).isFile(); },
      [](const QString& path) { return QFileInfo(path).canonicalFilePath(); });
  const auto command = px::modrim::restartCommand(program, originalArguments,
                                                  environment, keepQtScale);
  QProcess process;
  process.setProgram(command.program);
  process.setArguments(command.arguments);
  process.setProcessEnvironment(command.environment);
  if (!process.startDetached()) {
    spdlog::error("Could not restart PxModRim: {}",
                  process.errorString().toStdString());
    return false;
  }
  return true;
}

// Shuts UI plugins down, then the core context. Used once the app has started.
QCoro::Task<void> teardown(px::modrim::AppContext& appContext,
                           px::modrim::CoreContext& ctx) {
  if (auto shut = co_await appContext.shutdownAll(); !shut) {
    spdlog::error("Shutdown failed: {}", shut.error().message.toStdString());
  }
  co_await ctx.close();
}

// Startup, then waiting for the application to quit. Eager: it runs until the
// first suspension, which is the quit wait, so main can tell an early failure
// from a running app (see main()).
QCoro::Task<AppExit> runApp(QApplication& qtApp, QSet<QString> disabled,
                            px::modrim::ResolvedScale initialScale) {
  spdlog::info("Starting PxModRim");

  px::modrim::ConfigService configService(px::modrim::configDir());

  auto loadedCfg =
      configService.load<px::modrim::AppConfig>(px::modrim::kConfigFile);
  if (!loadedCfg) {
    spdlog::error("Failed to initialize configuration: {}",
                  loadedCfg.error().message.toStdString());
    co_return AppExit::kFailure;
  }
  auto uiPrefs = px::modrim::loadUiPrefs(configService);
  if (!uiPrefs) {
    spdlog::error("Failed to initialize UI preferences: {}",
                  uiPrefs.error().message.toStdString());
    co_return AppExit::kFailure;
  }

  px::modrim::AppConfig cfg = std::move(*loadedCfg);
  spdlog::info("Configuration loaded: game={}, local={}, workshop={}",
               cfg.paths.game, cfg.paths.local, cfg.paths.workshop);

  if (cfg.paths.game.empty()) {
    spdlog::info("No game path in config, attempting auto-detect");
    if (auto detected = px::modrim::detectGamePaths(); !detected.game.empty()) {
      cfg.paths = detected;
      if (auto saved = configService.save(px::modrim::kConfigFile, cfg);
          !saved) {
        spdlog::warn("Cannot save detected game paths: {}",
                     saved.error().message.toStdString());
      }
    }
  }

  // Declared before the context, so it outlives the organizer service that
  // holds it.
  px::modrim::organizer::OrganizerDb organizerDb(
      px::modrim::organizer::dbPath(configService.directory()));
  auto ctx = px::modrim::CoreContext::create(cfg, configService);
  const QStringList downloaders = px::modrim::registerCorePlugins(
      *ctx, configService, organizerDb, disabled);

  px::modrim::AppContext appContext(*ctx, *uiPrefs, initialScale);
  px::modrim::PluginSelection selection{std::move(disabled), downloaders};
  px::modrim::registerUiPlugins(appContext, selection);

  if (auto setup = appContext.setupAll(); !setup) {
    spdlog::error("Plugin setup failed: {}",
                  setup.error().message.toStdString());
    ctx->closeSync();
    co_return AppExit::kFailure;
  }
  spdlog::info("Services initialized for game version {}",
               ctx->gameVersion().toStdString());

  // The window opens before mods load, as in the legacy app.
  auto window = std::make_unique<px::modrim::MainWindow>(appContext);
  window->setAppQuitCallback([] { QCoreApplication::quit(); });
  window->show();

  if (ctx->config().paths.game.empty()) {
    spdlog::warn(
        "No game path configured; mods are not loaded until one is set");
  } else {
    auto refreshed = co_await appContext.refreshMods();
    if (!refreshed) {
      spdlog::error("Mod load failed: {}",
                    refreshed.error().message.toStdString());
      co_await teardown(appContext, *ctx);
      co_return AppExit::kFailure;
    }
    spdlog::info("Initial mod load complete: {} mods", *refreshed);

    if (auto init = co_await appContext.initAll(); !init) {
      spdlog::error("Plugin init failed: {}",
                    init.error().message.toStdString());
      co_await teardown(appContext, *ctx);
      co_return AppExit::kFailure;
    }
    window->startStartupUpdateCheck();
  }

  // Runs until the application quits (window close).
  co_await qCoro(&qtApp, &QCoreApplication::aboutToQuit);
  co_await teardown(appContext, *ctx);
  co_return window->restartRequested() ? AppExit::kRestart : AppExit::kSuccess;
}

}  // namespace

int main(int argc, char* argv[]) {
  setEnvDefaults();
  const QStringList originalArguments = commandLine(argc, argv);
  const QString cliScale = px::modrim::uiScaleArgument(originalArguments);
  const QString envScale = qEnvironmentVariable("PX_UI_SCALE");
  const auto initialScale = applyStartupScale(cliScale, envScale);

  QApplication qtApp(argc, argv);
  qtApp.setApplicationName(QStringLiteral("PxModRim"));
  qtApp.setApplicationVersion(px::modrim::appVersion());
  qtApp.setOrganizationName(QStringLiteral("PxModRim"));
  qtApp.setApplicationDisplayName(QStringLiteral("PxModRim"));
  qtApp.setDesktopFileName(QStringLiteral("pxmodrim"));
  qtApp.setWindowIcon(
      QIcon(QStringLiteral(":/qt/qml/PxModRim/assets/logo.svg")));
  // The app quits when its run finishes, not when the last window closes.
  qtApp.setQuitOnLastWindowClosed(false);
  qtApp.styleHints()->setWheelScrollLines(kWheelScrollLines);
  px::modrim::applyAppTheme(qtApp);
  qtApp.installEventFilter(new px::modrim::QuickPopupParenting(&qtApp));

  configureLogging(
      QDir(px::modrim::configDir()).filePath(QStringLiteral("logs")));
  warnInvalidScaleOverrides(cliScale, envScale);

  const QString manifest =
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("workshop-variant.txt"));
  auto disabled = px::modrim::parseDisabledPlugins(
      qEnvironmentVariable("PX_DISABLED_PLUGINS"), manifest);
  if (!disabled) {
    spdlog::critical("{}", disabled.error().message.toStdString());
    return 1;
  }

  auto run = runApp(qtApp, std::move(*disabled), initialScale);
  // An early failure has already finished the run; otherwise run the event loop
  // until quit.
  if (!run.isReady()) {
    qtApp.exec();
  }
  const auto result = QCoro::waitFor(std::move(run));
  if (result == AppExit::kRestart &&
      !relaunch(originalArguments,
                initialScale.source == px::modrim::ScaleSource::kQtEnv)) {
    return 1;
  }
  return result == AppExit::kFailure ? 1 : 0;
}
