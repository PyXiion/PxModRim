#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QCoro/QCoroSignal>
#include <QCoro/QCoroTask>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QStyleHints>
#include <QTimer>
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

Q_IMPORT_QML_PLUGIN(PxModRimPlugin)

namespace {

constexpr int kWheelScrollLines = 8;
constexpr qint64 kLogMaxBytes = 10 * 1024 * 1024;
constexpr std::size_t kLogRetention = 5;

// Qt6 defaults to PassThrough, which makes QtWebEngine render at integer buffer scale
// while the compositor upscales fractional scales, which blurs. RoundPreferFloor avoids
// the blur at the cost of slightly smaller UI (the QTBUG-113574 workaround).
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

// PX_LOG_LEVEL takes precedence over LOGURU_LEVEL (the legacy name). Unknown values
// and loguru's SUCCESS level fall back to info.
spdlog::level::level_enum logLevelFromEnv() {
    for (const char* name : {"PX_LOG_LEVEL", "LOGURU_LEVEL"}) {
        const std::string value = qEnvironmentVariable(name).trimmed().toLower().toStdString();
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

// Logs to stderr, and to a rotating file under the config directory when it can be opened.
void configureLogging(const QString& logsDir) {
    std::vector<spdlog::sink_ptr> sinks{std::make_shared<spdlog::sinks::stderr_color_sink_mt>()};
    auto logger = std::make_shared<spdlog::logger>("pxmodrim", sinks.begin(), sinks.end());
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e | %^%-8l%$ | %v");
    logger->set_level(logLevelFromEnv());
    spdlog::set_default_logger(logger);

    QDir().mkpath(logsDir);
    const std::string logFile = QDir(logsDir).filePath(QStringLiteral("pxmodrim.log")).toStdString();
    try {
        // spdlog reports an unopenable file by throwing; that is the only way it reports it.
        logger->sinks().push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFile, kLogMaxBytes, kLogRetention));
    } catch (const spdlog::spdlog_ex& ex) {
        spdlog::warn("Logging to {} is disabled: {}", logFile, ex.what());
    }
}

int smokeExitMs() {
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("PX_SMOKE_EXIT_MS", &ok);
    return ok ? ms : 0;
}

// Shuts UI plugins down, then the core context. Used once the app has started.
QCoro::Task<void> teardown(px::modrim::AppContext& appContext, px::modrim::CoreContext& ctx) {
    if (auto shut = co_await appContext.shutdownAll(); !shut) {
        spdlog::error("Shutdown failed: {}", shut.error().message.toStdString());
    }
    co_await ctx.close();
}

// Startup, then waiting for the application to quit. Eager: it runs until the first
// suspension, which is the quit wait, so main can tell an early failure from a
// running app (see main()).
QCoro::Task<int> runApp(QApplication& qtApp, QSet<QString> disabled) {
    spdlog::info("Starting PxModRim");

    px::modrim::ConfigService configService(px::modrim::configDir());
    px::modrim::AppConfigService appConfigService(configService);
    px::modrim::UiPrefsService uiPrefsService(configService);

    if (auto setup = co_await appConfigService.setup(); !setup) {
        spdlog::error("Failed to initialize configuration: {}", setup.error().message.toStdString());
        co_return 1;
    }
    if (auto setup = co_await uiPrefsService.setup(); !setup) {
        spdlog::error("Failed to initialize UI preferences: {}", setup.error().message.toStdString());
        co_return 1;
    }

    px::modrim::AppConfig cfg = appConfigService.cfg();
    spdlog::info("Configuration loaded: game={}, local={}, workshop={}", cfg.paths.game, cfg.paths.local,
                 cfg.paths.workshop);

    if (cfg.paths.game.empty()) {
        spdlog::info("No game path in config, attempting auto-detect");
        if (auto detected = px::modrim::detectGamePaths(); !detected.game.empty()) {
            cfg.paths = detected;
            if (auto saved = configService.save(QStringLiteral("config.json"), cfg); !saved) {
                spdlog::warn("Cannot save detected game paths: {}", saved.error().message.toStdString());
            }
        }
    }

    // Declared before the context, so it outlives the organizer service that holds it.
    px::modrim::organizer::OrganizerDb organizerDb(px::modrim::organizer::dbPath(configService.directory()));
    auto ctx = px::modrim::CoreContext::create(cfg, &configService);
    const QStringList downloaders = px::modrim::registerCorePlugins(*ctx, configService, organizerDb, disabled);

    px::modrim::AppContext appContext(*ctx, uiPrefsService.prefs());
    px::modrim::PluginSelection selection{std::move(disabled), downloaders};
    px::modrim::registerUiPlugins(appContext, selection);

    if (auto setup = appContext.setupAll(); !setup) {
        spdlog::error("Plugin setup failed: {}", setup.error().message.toStdString());
        ctx->closeSync();
        co_return 1;
    }
    spdlog::info("Services initialized for game version {}", ctx->gameVersion().toStdString());

    // The window opens before mods load, as in the legacy app.
    auto window = std::make_unique<px::modrim::MainWindow>(appContext);
    window->setAppQuitCallback([] { QCoreApplication::quit(); });
    window->show();

    if (ctx->config().paths.game.empty()) {
        spdlog::warn("No game path configured; mods are not loaded until one is set");
    } else {
        auto refreshed = co_await appContext.refreshMods();
        if (!refreshed) {
            spdlog::error("Mod load failed: {}", refreshed.error().message.toStdString());
            co_await teardown(appContext, *ctx);
            co_return 1;
        }
        spdlog::info("Initial mod load complete: {} mods", *refreshed);

        if (auto init = co_await appContext.initAll(); !init) {
            spdlog::error("Plugin init failed: {}", init.error().message.toStdString());
            co_await teardown(appContext, *ctx);
            co_return 1;
        }
        window->startStartupUpdateCheck();
    }

    // Runs until the application quits (window close, or PX_SMOKE_EXIT_MS).
    co_await qCoro(&qtApp, &QCoreApplication::aboutToQuit);
    co_await teardown(appContext, *ctx);
    co_return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    setEnvDefaults();

    QApplication qtApp(argc, argv);
    qtApp.setApplicationName(QStringLiteral("PxModRim"));
    qtApp.setApplicationVersion(px::modrim::appVersion());
    qtApp.setOrganizationName(QStringLiteral("PxModRim"));
    qtApp.setApplicationDisplayName(QStringLiteral("PxModRim"));
    qtApp.setDesktopFileName(QStringLiteral("pxmodrim"));
    qtApp.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/PxModRim/assets/logo.svg")));
    // The app quits when its run finishes, not when the last window closes.
    qtApp.setQuitOnLastWindowClosed(false);
    qtApp.styleHints()->setWheelScrollLines(kWheelScrollLines);
    px::modrim::applyAppTheme(qtApp);
    qtApp.installEventFilter(new px::modrim::QuickPopupParenting(&qtApp));

    configureLogging(QDir(px::modrim::configDir()).filePath(QStringLiteral("logs")));

    if (const int smokeMs = smokeExitMs(); smokeMs > 0) {
        QTimer::singleShot(smokeMs, &qtApp, &QCoreApplication::quit);
    }

    const QString manifest =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("workshop-variant.txt"));
    auto disabled = px::modrim::parseDisabledPlugins(qEnvironmentVariable("PX_DISABLED_PLUGINS"), manifest);
    if (!disabled) {
        spdlog::critical("{}", disabled.error().message.toStdString());
        return 1;
    }

    auto run = runApp(qtApp, std::move(*disabled));
    // An early failure has already finished the run; otherwise run the event loop until quit.
    if (!run.isReady()) {
        qtApp.exec();
    }
    return QCoro::waitFor(std::move(run));
}
