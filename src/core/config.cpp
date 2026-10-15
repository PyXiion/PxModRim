#include "core/config.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <spdlog/spdlog.h>

#include "core/constants.h"

namespace px::core {

namespace {

const QString kConfigFile = QStringLiteral("config.json");
const QString kUiPrefsFile = QStringLiteral("ui_prefs.json");

// Schema versions above this are far beyond any real file; clamping keeps them
// representable while still being rejected as unsupported.
constexpr double kMaxSchemaVersion = 1e15;

QString steam_app_id() { return QString(RIMWORLD_STEAM_APP_ID); }

QString join_path(QString path, std::initializer_list<QString> parts) {
    for (const auto& part : parts) {
        path.append(u'/').append(part);
    }
    return QDir::cleanPath(path);
}

bool is_dir(const QString& path) { return QFileInfo(path).isDir(); }

QString find_rimworld_in_steam_root(const QString& root, const QString& steam_id);

// Parses a libraryfolders.vdf for an alternate library holding RimWorld.
QString vdf_find_rimworld(const QString& vdf_path, const QString& steam_id) {
    QFile file(vdf_path);
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::warn("Failed to parse libraryfolders.vdf");
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());

    // Matches `"path" "<library>"` and `"<appid>" {` lines of libraryfolders.vdf.
    static const QRegularExpression path_pattern(QStringLiteral(R"re("path"\s+"(.+?)")re"));
    const QRegularExpression app_pattern(QStringLiteral("\"") + QRegularExpression::escape(steam_id) +
                                         QStringLiteral(R"re("\s*\{)re"));

    QString current_path;
    for (const auto& raw_line : text.split(u'\n')) {
        const QString line = raw_line.trimmed();
        if (const auto match = path_pattern.match(line); match.hasMatch()) {
            current_path = match.captured(1);
            continue;
        }
        if (!current_path.isEmpty() && app_pattern.match(line).hasMatch()) {
            const QString game = join_path(current_path, {QStringLiteral("steamapps"),
                                                          QStringLiteral("common"),
                                                          QStringLiteral("RimWorld")});
            if (is_dir(game)) {
                return game;
            }
        }
    }
    return {};
}

QString find_rimworld_in_steam_root(const QString& root, const QString& steam_id) {
    const QString game = join_path(root, {QStringLiteral("steamapps"), QStringLiteral("common"),
                                          QStringLiteral("RimWorld")});
    if (is_dir(game)) {
        return game;
    }
    const QString config_vdf = join_path(root, {QStringLiteral("config"), QStringLiteral("libraryfolders.vdf")});
    if (QFileInfo::exists(config_vdf)) {
        return vdf_find_rimworld(config_vdf, steam_id);
    }
    const QString steamapps_vdf =
        join_path(root, {QStringLiteral("steamapps"), QStringLiteral("libraryfolders.vdf")});
    if (QFileInfo::exists(steamapps_vdf)) {
        return vdf_find_rimworld(steamapps_vdf, steam_id);
    }
    return {};
}

// Workshop content lives beside the `common` folder that holds the game.
QString workshop_path_from_game(const QString& game, const QString& steam_root, const QString& steam_id) {
    const QString normalized = QDir::fromNativeSeparators(game);
    const QStringList parts = normalized.split(u'/', Qt::SkipEmptyParts);
    const qsizetype common = parts.indexOf(QStringLiteral("common"));
    if (common >= 0) {
        QString base = parts.mid(0, common).join(u'/');
        if (normalized.startsWith(u'/')) {
            base.prepend(u'/');
        }
        const QString workshop =
            join_path(base, {QStringLiteral("workshop"), QStringLiteral("content"), steam_id});
        if (is_dir(workshop)) {
            return workshop;
        }
    }
    return join_path(steam_root, {QStringLiteral("steamapps"), QStringLiteral("workshop"),
                                  QStringLiteral("content"), steam_id});
}

struct GamePaths {
    QString game;
    QString local;
    QString workshop;
};

GamePaths game_paths_for(const QString& game, const QString& workshop) {
    return GamePaths{
        .game = game,
        .local = join_path(game, {QStringLiteral("Mods")}),
        .workshop = workshop,
    };
}

#if defined(Q_OS_LINUX)
GamePaths find_game_paths(QString& found_root) {
    const QString steam_id = steam_app_id();
    const QString home = QDir::homePath();
    const QStringList candidates = {
        join_path(home, {QStringLiteral(".steam"), QStringLiteral("debian-installation")}),
        join_path(home, {QStringLiteral(".steam"), QStringLiteral("steam")}),
        join_path(home, {QStringLiteral(".local"), QStringLiteral("share"), QStringLiteral("Steam")}),
        join_path(home, {QStringLiteral(".var"), QStringLiteral("app"), QStringLiteral("com.valvesoftware.Steam"),
                         QStringLiteral(".local"), QStringLiteral("share"), QStringLiteral("Steam")}),
        join_path(home, {QStringLiteral("snap"), QStringLiteral("steam"), QStringLiteral("common"),
                         QStringLiteral(".local"), QStringLiteral("share"), QStringLiteral("Steam")}),
    };
    for (const auto& root : candidates) {
        if (!is_dir(root)) {
            continue;
        }
        if (const QString game = find_rimworld_in_steam_root(root, steam_id); !game.isEmpty()) {
            found_root = root;
            return game_paths_for(game, workshop_path_from_game(game, root, steam_id));
        }
    }

    const QString fallback_root = join_path(home, {QStringLiteral(".steam"), QStringLiteral("steam")});
    const QString fallback =
        join_path(fallback_root, {QStringLiteral("steamapps"), QStringLiteral("common"), QStringLiteral("RimWorld")});
    if (is_dir(fallback)) {
        found_root = is_dir(fallback_root) ? fallback_root : QString();
        return game_paths_for(fallback, join_path(fallback_root, {QStringLiteral("steamapps"),
                                                                 QStringLiteral("workshop"),
                                                                 QStringLiteral("content"), steam_id}));
    }
    return {};
}
#elif defined(Q_OS_MACOS)
QString find_mac_app(const QString& rimworld_dir) {
    if (!is_dir(rimworld_dir)) {
        return {};
    }
    const QStringList apps = QDir(rimworld_dir)
                                 .entryList({QStringLiteral("*.app")},
                                            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    return apps.isEmpty() ? QString() : join_path(rimworld_dir, {apps.first()});
}

GamePaths find_game_paths(QString& found_root) {
    const QString steam_id = steam_app_id();
    const QString steam_root =
        join_path(QDir::homePath(), {QStringLiteral("Library"), QStringLiteral("Application Support"),
                                     QStringLiteral("Steam")});
    found_root = is_dir(steam_root) ? steam_root : QString();
    if (const QString game = find_rimworld_in_steam_root(steam_root, steam_id); !game.isEmpty()) {
        return game_paths_for(game, workshop_path_from_game(game, steam_root, steam_id));
    }
    const QString fallback =
        join_path(steam_root, {QStringLiteral("steamapps"), QStringLiteral("common"), QStringLiteral("RimWorld")});
    if (const QString app_bundle = find_mac_app(fallback); !app_bundle.isEmpty()) {
        return GamePaths{
            .game = app_bundle,
            .local = join_path(fallback, {QStringLiteral("Mods")}),
            .workshop = join_path(steam_root, {QStringLiteral("steamapps"), QStringLiteral("workshop"),
                                               QStringLiteral("content"), steam_id}),
        };
    }
    return {};
}
#elif defined(Q_OS_WIN)
// Steam's install location is recorded in the registry; the 32-bit view is
// checked first, as the legacy detection did.
GamePaths find_game_paths(QString& found_root) {
    const QString steam_id = steam_app_id();
    const QStringList registry_keys = {
        QStringLiteral(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Valve\Steam)"),
        QStringLiteral(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam)"),
    };
    for (const auto& key : registry_keys) {
        const QString steam_root = QSettings(key, QSettings::NativeFormat).value(QStringLiteral("InstallPath")).toString();
        if (steam_root.isEmpty()) {
            spdlog::debug("Unable to inspect Windows Steam registry key {}", key.toStdString());
            continue;
        }
        if (const QString game = find_rimworld_in_steam_root(steam_root, steam_id); !game.isEmpty()) {
            return game_paths_for(game, workshop_path_from_game(game, steam_root, steam_id));
        }
    }

    const QString fallback = QStringLiteral("C:/Program Files (x86)/Steam");
    const QString game =
        join_path(fallback, {QStringLiteral("steamapps"), QStringLiteral("common"), QStringLiteral("RimWorld")});
    if (is_dir(game)) {
        return game_paths_for(game, join_path(fallback, {QStringLiteral("steamapps"), QStringLiteral("workshop"),
                                                         QStringLiteral("content"), steam_id}));
    }
    return {};
}
#else
GamePaths find_game_paths(QString&) { return {}; }
#endif

// Locates the RimWorld Config folder, which the game writes beside its save data.
QString detect_config_folder([[maybe_unused]] const QString& steam_root) {
#if defined(Q_OS_LINUX)
    const QString steam_id = steam_app_id();
    if (!steam_root.isEmpty()) {
        const QString proton =
            join_path(steam_root, {QStringLiteral("steamapps"), QStringLiteral("compatdata"), steam_id,
                                   QStringLiteral("pfx"), QStringLiteral("drive_c"), QStringLiteral("users"),
                                   QStringLiteral("steamuser"), QStringLiteral("AppData"),
                                   QStringLiteral("LocalLow"), QStringLiteral("Ludeon Studios"),
                                   QStringLiteral("RimWorld by Ludeon Studios"), QStringLiteral("Config")});
        if (is_dir(proton)) {
            spdlog::debug("Detected Proton config folder: {}", proton.toStdString());
            return proton;
        }
    }
    const QString native = join_path(QDir::homePath(), {QStringLiteral(".config"), QStringLiteral("unity3d"),
                                                        QStringLiteral("Ludeon Studios"),
                                                        QStringLiteral("RimWorld by Ludeon Studios"),
                                                        QStringLiteral("Config")});
    if (is_dir(native)) {
        spdlog::debug("Detected native config folder: {}", native.toStdString());
        return native;
    }
#elif defined(Q_OS_MACOS)
    const QString config = join_path(QDir::homePath(), {QStringLiteral("Library"),
                                                        QStringLiteral("Application Support"),
                                                        QStringLiteral("Rimworld"), QStringLiteral("Config")});
    if (is_dir(config)) {
        spdlog::debug("Detected macOS config folder: {}", config.toStdString());
        return config;
    }
#elif defined(Q_OS_WIN)
    const QString config = join_path(QDir::homePath(), {QStringLiteral("AppData"), QStringLiteral("LocalLow"),
                                                        QStringLiteral("Ludeon Studios"),
                                                        QStringLiteral("RimWorld by Ludeon Studios"),
                                                        QStringLiteral("Config")});
    if (is_dir(config)) {
        spdlog::debug("Detected Windows config folder: {}", config.toStdString());
        return config;
    }
#endif
    return {};
}

}  // namespace

ConfigService::ConfigService(QString config_dir, JsonMigrations migrations)
    : dir_(std::move(config_dir)), migrations_(std::move(migrations)) {}

const QString& ConfigService::directory() const noexcept { return dir_; }

QString ConfigService::file_path(const QString& filename) const {
    return QDir(dir_).filePath(filename);
}

bool ConfigService::is_versioned(const QString& filename) {
    return filename == kConfigFile || filename == kUiPrefsFile;
}

std::string ConfigService::with_schema_marker(std::string pretty_object) {
    // Pretty output ends with the object's closing brace; the marker goes after the
    // last non-space character before it, which is the last member's value or the opening brace.
    const auto close = pretty_object.rfind('}');
    if (close == std::string::npos) {
        return pretty_object;
    }
    auto last = pretty_object.find_last_not_of(" \t\r\n", close - 1);
    if (last == std::string::npos) {
        return pretty_object;
    }
    const bool empty_object = pretty_object[last] == '{';
    const std::string marker = std::string("\"") + std::string(kSchemaVersionKey) + "\": " +
                               std::to_string(kCurrentConfigSchemaVersion);
    pretty_object.insert(last + 1, empty_object ? "\n  " + marker + "\n" : ",\n  " + marker);
    return pretty_object;
}

JsonObject ConfigService::load_raw(const QString& filename) const {
    auto bytes = read_file_bytes(file_path(filename));
    if (!bytes) {
        return {};
    }
    glz::json_t document;
    if (glz::read_json(document, *bytes) || !document.is_object()) {
        return {};
    }
    return document.get_object();
}

Result<std::optional<JsonObject>> ConfigService::load_versioned(const QString& filename) const {
    const QString path = file_path(filename);
    auto bytes = read_file_bytes(path);
    if (!bytes) {
        spdlog::warn("Failed to load {}: {}", filename.toStdString(), bytes.error().message.toStdString());
        return std::optional<JsonObject>{};
    }

    glz::json_t document;
    if (auto error = glz::read_json(document, *bytes)) {
        spdlog::warn("Failed to load {}: {}", filename.toStdString(), glz::format_error(error, *bytes));
        return std::optional<JsonObject>{};
    }
    if (!document.is_object()) {
        spdlog::warn("Failed to load {}: top-level value is not an object", filename.toStdString());
        return std::optional<JsonObject>{};
    }
    JsonObject raw = document.get_object();

    // A missing marker or an explicit null means schema version 0.
    std::int64_t version = 0;
    if (auto marker = raw.find(std::string(kSchemaVersionKey)); marker != raw.end()) {
        const auto& value = marker->second;
        if (!value.is_null()) {
            const bool valid = value.is_number() && value.get<double>() >= 0 &&
                               std::trunc(value.get<double>()) == value.get<double>();
            if (!valid) {
                spdlog::warn("Failed to load {}: Invalid JSON schema version", filename.toStdString());
                return std::optional<JsonObject>{};
            }
            version = static_cast<std::int64_t>(std::min(value.get<double>(), kMaxSchemaVersion));
        }
        raw.erase(marker);
    }

    if (auto migrated = migrate(raw, version); !migrated) {
        return std::unexpected(migrated.error());
    }
    if (version == kCurrentConfigSchemaVersion) {
        return std::optional<JsonObject>(std::move(raw));
    }

    // Keep the pre-migration file as a backup before the rewritten one replaces it.
    const QString backup = path + QStringLiteral(".bak.") + QString::number(QDateTime::currentSecsSinceEpoch());
    if (auto copied = copy_file_replacing(path, backup); !copied) {
        spdlog::warn("Failed to load {}: {}", filename.toStdString(), copied.error().message.toStdString());
        return std::optional<JsonObject>{};
    }
    raw[std::string(kSchemaVersionKey)] = static_cast<double>(kCurrentConfigSchemaVersion);
    auto text = glz::write<kJsonPrettyOpts>(raw);
    if (!text) {
        spdlog::warn("Failed to load {}: cannot encode migrated document", filename.toStdString());
        return std::optional<JsonObject>{};
    }
    if (auto written = write_file_atomically(path, *text); !written) {
        spdlog::warn("Failed to load {}: {}", filename.toStdString(), written.error().message.toStdString());
        return std::optional<JsonObject>{};
    }
    return std::optional<JsonObject>(std::move(raw));
}

Result<void> ConfigService::migrate(JsonObject& data, std::int64_t current) const {
    if (current > kCurrentConfigSchemaVersion) {
        return std::unexpected(Error{QStringLiteral("Unsupported JSON schema version %1; latest is %2")
                                         .arg(current)
                                         .arg(kCurrentConfigSchemaVersion)});
    }
    for (std::int64_t target = current + 1; target <= kCurrentConfigSchemaVersion; ++target) {
        const auto migration = migrations_.find(static_cast<int>(target));
        if (migration == migrations_.end()) {
            return std::unexpected(
                Error{QStringLiteral("Missing JSON migration step for target version %1").arg(target)});
        }
        migration->second(data);
    }
    return {};
}

QString config_dir() {
    const QString base = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config"));
    return QDir(base).filePath(QStringLiteral("pxmodrim"));
}

QString community_rules_file() {
    return QDir(config_dir()).filePath(QStringLiteral("communityRules.json"));
}

std::optional<QString> read_game_version(const QString& game_path) {
    const QString path = QDir(game_path).filePath(QStringLiteral("Version.txt"));
    if (!QFileInfo::exists(path)) {
        return std::nullopt;
    }
    auto bytes = read_file_bytes(path);
    if (!bytes) {
        spdlog::warn("Failed to read game version from {}", path.toStdString());
        return std::nullopt;
    }
    return QString::fromUtf8(bytes->data(), static_cast<qsizetype>(bytes->size())).trimmed();
}

PathConfig detect_game_paths() {
    QString found_root;
    const GamePaths found = find_game_paths(found_root);

    PathConfig result;
    result.game = found.game.toStdString();
    result.local = found.local.toStdString();
    result.workshop = found.workshop.toStdString();
    result.config_folder = detect_config_folder(found_root).toStdString();
    result.community_rules_file = community_rules_file().toStdString();
    return result;
}

AppConfigService::AppConfigService(ConfigService& svc) : svc_(svc) {}

QCoro::Task<Result<void>> AppConfigService::setup() {
    auto loaded = svc_.load<AppConfig>(kConfigFile);
    if (!loaded) {
        co_return std::unexpected(loaded.error());
    }
    cfg_ = std::move(*loaded);
    co_return {};
}

const AppConfig& AppConfigService::cfg() const {
    Q_ASSERT(cfg_.has_value());
    return *cfg_;
}

Result<void> AppConfigService::save_cfg() const {
    if (!cfg_) {
        return {};
    }
    return svc_.save(kConfigFile, *cfg_);
}

}  // namespace px::core
