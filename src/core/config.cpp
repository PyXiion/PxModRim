#include "core/config.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <spdlog/spdlog.h>

#include "core/constants.hpp"

namespace px::modrim {

namespace {

const QString kConfigFile = QStringLiteral("config.json");
const QString kUiPrefsFile = QStringLiteral("ui_prefs.json");

// Schema versions above this are far beyond any real file; clamping keeps them
// representable while still being rejected as unsupported.
constexpr double kMaxSchemaVersion = 1e15;

QString steamAppId() { return QString(kRimworldSteamAppId); }

QString joinPath(QString path, std::initializer_list<QString> parts) {
  for (const auto& part : parts) {
    path.append(u'/').append(part);
  }
  return QDir::cleanPath(path);
}

bool isDir(const QString& path) { return QFileInfo(path).isDir(); }

QString findRimworldInSteamRoot(const QString& root, const QString& steamId);

// Parses a libraryfolders.vdf for an alternate library holding RimWorld.
QString vdfFindRimworld(const QString& vdfPath, const QString& steamId) {
  QFile file(vdfPath);
  if (!file.open(QIODevice::ReadOnly)) {
    spdlog::warn("Failed to parse libraryfolders.vdf");
    return {};
  }
  const QString text = QString::fromUtf8(file.readAll());

  // Matches `"path" "<library>"` and `"<appid>" {` lines of libraryfolders.vdf.
  static const QRegularExpression pathPattern(
      QStringLiteral(R"re("path"\s+"(.+?)")re"));
  const QRegularExpression appPattern(QStringLiteral("\"") +
                                      QRegularExpression::escape(steamId) +
                                      QStringLiteral(R"re("\s*\{)re"));

  QString currentPath;
  for (const auto& rawLine : text.split(u'\n')) {
    const QString line = rawLine.trimmed();
    if (const auto match = pathPattern.match(line); match.hasMatch()) {
      currentPath = match.captured(1);
      continue;
    }
    if (!currentPath.isEmpty() && appPattern.match(line).hasMatch()) {
      const QString game = joinPath(
          currentPath, {QStringLiteral("steamapps"), QStringLiteral("common"),
                        QStringLiteral("RimWorld")});
      if (isDir(game)) {
        return game;
      }
    }
  }
  return {};
}

QString findRimworldInSteamRoot(const QString& root, const QString& steamId) {
  const QString game =
      joinPath(root, {QStringLiteral("steamapps"), QStringLiteral("common"),
                      QStringLiteral("RimWorld")});
  if (isDir(game)) {
    return game;
  }
  const QString configVdf = joinPath(
      root, {QStringLiteral("config"), QStringLiteral("libraryfolders.vdf")});
  if (QFileInfo::exists(configVdf)) {
    return vdfFindRimworld(configVdf, steamId);
  }
  const QString steamappsVdf = joinPath(
      root,
      {QStringLiteral("steamapps"), QStringLiteral("libraryfolders.vdf")});
  if (QFileInfo::exists(steamappsVdf)) {
    return vdfFindRimworld(steamappsVdf, steamId);
  }
  return {};
}

// Workshop content lives beside the `common` folder that holds the game.
QString workshopPathFromGame(const QString& game, const QString& steamRoot,
                             const QString& steamId) {
  const QString normalized = QDir::fromNativeSeparators(game);
  const QStringList parts = normalized.split(u'/', Qt::SkipEmptyParts);
  const qsizetype common = parts.indexOf(QStringLiteral("common"));
  if (common >= 0) {
    QString base = parts.mid(0, common).join(u'/');
    if (normalized.startsWith(u'/')) {
      base.prepend(u'/');
    }
    const QString workshop = joinPath(
        base, {QStringLiteral("workshop"), QStringLiteral("content"), steamId});
    if (isDir(workshop)) {
      return workshop;
    }
  }
  return joinPath(steamRoot,
                  {QStringLiteral("steamapps"), QStringLiteral("workshop"),
                   QStringLiteral("content"), steamId});
}

struct GamePaths {
  QString game;
  QString local;
  QString workshop;
};

GamePaths gamePathsFor(const QString& game, const QString& workshop) {
  return GamePaths{
      .game = game,
      .local = joinPath(game, {QStringLiteral("Mods")}),
      .workshop = workshop,
  };
}

#if defined(Q_OS_LINUX)
GamePaths findGamePaths(QString& foundRoot) {
  const QString steamId = steamAppId();
  const QString home = QDir::homePath();
  const QStringList candidates = {
      joinPath(home, {QStringLiteral(".steam"),
                      QStringLiteral("debian-installation")}),
      joinPath(home, {QStringLiteral(".steam"), QStringLiteral("steam")}),
      joinPath(home, {QStringLiteral(".local"), QStringLiteral("share"),
                      QStringLiteral("Steam")}),
      joinPath(home, {QStringLiteral(".var"), QStringLiteral("app"),
                      QStringLiteral("com.valvesoftware.Steam"),
                      QStringLiteral(".local"), QStringLiteral("share"),
                      QStringLiteral("Steam")}),
      joinPath(home, {QStringLiteral("snap"), QStringLiteral("steam"),
                      QStringLiteral("common"), QStringLiteral(".local"),
                      QStringLiteral("share"), QStringLiteral("Steam")}),
  };
  for (const auto& root : candidates) {
    if (!isDir(root)) {
      continue;
    }
    if (const QString game = findRimworldInSteamRoot(root, steamId);
        !game.isEmpty()) {
      foundRoot = root;
      return gamePathsFor(game, workshopPathFromGame(game, root, steamId));
    }
  }

  const QString fallbackRoot =
      joinPath(home, {QStringLiteral(".steam"), QStringLiteral("steam")});
  const QString fallback = joinPath(
      fallbackRoot, {QStringLiteral("steamapps"), QStringLiteral("common"),
                     QStringLiteral("RimWorld")});
  if (isDir(fallback)) {
    foundRoot = isDir(fallbackRoot) ? fallbackRoot : QString();
    return gamePathsFor(
        fallback, joinPath(fallbackRoot, {QStringLiteral("steamapps"),
                                          QStringLiteral("workshop"),
                                          QStringLiteral("content"), steamId}));
  }
  return {};
}
#elif defined(Q_OS_MACOS)
QString find_mac_app(const QString& rimworld_dir) {
  if (!isDir(rimworld_dir)) {
    return {};
  }
  const QStringList apps =
      QDir(rimworld_dir)
          .entryList({QStringLiteral("*.app")},
                     QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                     QDir::Name);
  return apps.isEmpty() ? QString() : joinPath(rimworld_dir, {apps.first()});
}

GamePaths findGamePaths(QString& foundRoot) {
  const QString steamId = steamAppId();
  const QString steamRoot =
      joinPath(QDir::homePath(), {QStringLiteral("Library"),
                                  QStringLiteral("Application Support"),
                                  QStringLiteral("Steam")});
  foundRoot = isDir(steamRoot) ? steamRoot : QString();
  if (const QString game = findRimworldInSteamRoot(steamRoot, steamId);
      !game.isEmpty()) {
    return gamePathsFor(game, workshopPathFromGame(game, steamRoot, steamId));
  }
  const QString fallback = joinPath(
      steamRoot, {QStringLiteral("steamapps"), QStringLiteral("common"),
                  QStringLiteral("RimWorld")});
  if (const QString app_bundle = find_mac_app(fallback);
      !app_bundle.isEmpty()) {
    return GamePaths{
        .game = app_bundle,
        .local = joinPath(fallback, {QStringLiteral("Mods")}),
        .workshop = joinPath(
            steamRoot, {QStringLiteral("steamapps"), QStringLiteral("workshop"),
                        QStringLiteral("content"), steamId}),
    };
  }
  return {};
}
#elif defined(Q_OS_WIN)
// Steam's install location is recorded in the registry; the 32-bit view is
// checked first, as the legacy detection did.
GamePaths findGamePaths(QString& foundRoot) {
  const QString steamId = steamAppId();
  const QStringList registry_keys = {
      QStringLiteral(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Valve\Steam)"),
      QStringLiteral(R"(HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam)"),
  };
  for (const auto& key : registry_keys) {
    const QString steamRoot = QSettings(key, QSettings::NativeFormat)
                                  .value(QStringLiteral("InstallPath"))
                                  .toString();
    if (steamRoot.isEmpty()) {
      spdlog::debug("Unable to inspect Windows Steam registry key {}",
                    key.toStdString());
      continue;
    }
    if (const QString game = findRimworldInSteamRoot(steamRoot, steamId);
        !game.isEmpty()) {
      return gamePathsFor(game, workshopPathFromGame(game, steamRoot, steamId));
    }
  }

  const QString fallback = QStringLiteral("C:/Program Files (x86)/Steam");
  const QString game =
      joinPath(fallback, {QStringLiteral("steamapps"), QStringLiteral("common"),
                          QStringLiteral("RimWorld")});
  if (isDir(game)) {
    return gamePathsFor(
        game, joinPath(fallback,
                       {QStringLiteral("steamapps"), QStringLiteral("workshop"),
                        QStringLiteral("content"), steamId}));
  }
  return {};
}
#else
GamePaths findGamePaths(QString&) { return {}; }
#endif

// Locates the RimWorld Config folder, which the game writes beside its save
// data.
QString detectConfigFolder([[maybe_unused]] const QString& steamRoot) {
#if defined(Q_OS_LINUX)
  const QString steamId = steamAppId();
  if (!steamRoot.isEmpty()) {
    const QString proton = joinPath(
        steamRoot, {QStringLiteral("steamapps"), QStringLiteral("compatdata"),
                    steamId, QStringLiteral("pfx"), QStringLiteral("drive_c"),
                    QStringLiteral("users"), QStringLiteral("steamuser"),
                    QStringLiteral("AppData"), QStringLiteral("LocalLow"),
                    QStringLiteral("Ludeon Studios"),
                    QStringLiteral("RimWorld by Ludeon Studios"),
                    QStringLiteral("Config")});
    if (isDir(proton)) {
      spdlog::debug("Detected Proton config folder: {}", proton.toStdString());
      return proton;
    }
  }
  const QString native = joinPath(
      QDir::homePath(),
      {QStringLiteral(".config"), QStringLiteral("unity3d"),
       QStringLiteral("Ludeon Studios"),
       QStringLiteral("RimWorld by Ludeon Studios"), QStringLiteral("Config")});
  if (isDir(native)) {
    spdlog::debug("Detected native config folder: {}", native.toStdString());
    return native;
  }
#elif defined(Q_OS_MACOS)
  const QString config = joinPath(
      QDir::homePath(),
      {QStringLiteral("Library"), QStringLiteral("Application Support"),
       QStringLiteral("Rimworld"), QStringLiteral("Config")});
  if (isDir(config)) {
    spdlog::debug("Detected macOS config folder: {}", config.toStdString());
    return config;
  }
#elif defined(Q_OS_WIN)
  const QString config = joinPath(
      QDir::homePath(),
      {QStringLiteral("AppData"), QStringLiteral("LocalLow"),
       QStringLiteral("Ludeon Studios"),
       QStringLiteral("RimWorld by Ludeon Studios"), QStringLiteral("Config")});
  if (isDir(config)) {
    spdlog::debug("Detected Windows config folder: {}", config.toStdString());
    return config;
  }
#endif
  return {};
}

}  // namespace

ConfigService::ConfigService(QString configDir, JsonMigrations migrations)
    : m_dir(std::move(configDir)), m_migrations(std::move(migrations)) {}

const QString& ConfigService::directory() const noexcept { return m_dir; }

QString ConfigService::filePath(const QString& filename) const {
  return QDir(m_dir).filePath(filename);
}

bool ConfigService::isVersioned(const QString& filename) {
  return filename == kConfigFile || filename == kUiPrefsFile;
}

std::string ConfigService::withSchemaMarker(std::string prettyObject) {
  // Pretty output ends with the object's closing brace; the marker goes after
  // the last non-space character before it, which is the last member's value or
  // the opening brace.
  const auto close = prettyObject.rfind('}');
  if (close == std::string::npos) {
    return prettyObject;
  }
  auto last = prettyObject.find_last_not_of(" \t\r\n", close - 1);
  if (last == std::string::npos) {
    return prettyObject;
  }
  const bool emptyObject = prettyObject[last] == '{';
  const std::string marker =
      std::string("\"") + std::string(kSchemaVersionKey) +
      "\": " + std::to_string(kCurrentConfigSchemaVersion);
  prettyObject.insert(last + 1,
                      emptyObject ? "\n  " + marker + "\n" : ",\n  " + marker);
  return prettyObject;
}

JsonObject ConfigService::loadRaw(const QString& filename) const {
  auto bytes = readFileBytes(filePath(filename));
  if (!bytes) {
    return {};
  }
  glz::json_t document;
  if (glz::read_json(document, *bytes) || !document.is_object()) {
    return {};
  }
  return std::move(document.get_object());
}

Result<std::optional<JsonObject>> ConfigService::loadVersioned(
    const QString& filename) const {
  const QString path = filePath(filename);
  auto bytes = readFileBytes(path);
  if (!bytes) {
    spdlog::warn("Failed to load {}: {}", filename.toStdString(),
                 bytes.error().message.toStdString());
    return std::optional<JsonObject>{};
  }

  glz::json_t document;
  if (auto error = glz::read_json(document, *bytes)) {
    spdlog::warn("Failed to load {}: {}", filename.toStdString(),
                 glz::format_error(error, *bytes));
    return std::optional<JsonObject>{};
  }
  if (!document.is_object()) {
    spdlog::warn("Failed to load {}: top-level value is not an object",
                 filename.toStdString());
    return std::optional<JsonObject>{};
  }
  JsonObject raw = std::move(document.get_object());

  // A missing marker or an explicit null means schema version 0.
  std::int64_t version = 0;
  if (auto marker = raw.find(std::string(kSchemaVersionKey));
      marker != raw.end()) {
    const auto& value = marker->second;
    if (!value.is_null()) {
      const bool valid = value.is_number() && value.get<double>() >= 0 &&
                         std::trunc(value.get<double>()) == value.get<double>();
      if (!valid) {
        spdlog::warn("Failed to load {}: Invalid JSON schema version",
                     filename.toStdString());
        return std::optional<JsonObject>{};
      }
      version = static_cast<std::int64_t>(
          std::min(value.get<double>(), kMaxSchemaVersion));
    }
    raw.erase(marker);
  }

  if (auto migrated = migrate(raw, version); !migrated) {
    return std::unexpected(migrated.error());
  }
  if (version == kCurrentConfigSchemaVersion) {
    return std::optional<JsonObject>(std::move(raw));
  }

  // Keep the pre-migration file as a backup before the rewritten one replaces
  // it.
  const QString backup = path + QStringLiteral(".bak.") +
                         QString::number(QDateTime::currentSecsSinceEpoch());
  if (auto copied = copyFileReplacing(path, backup); !copied) {
    spdlog::warn("Failed to load {}: {}", filename.toStdString(),
                 copied.error().message.toStdString());
    return std::optional<JsonObject>{};
  }
  raw[std::string(kSchemaVersionKey)] =
      static_cast<double>(kCurrentConfigSchemaVersion);
  auto text = glz::write<kJsonPrettyOpts>(raw);
  if (!text) {
    spdlog::warn("Failed to load {}: cannot encode migrated document",
                 filename.toStdString());
    return std::optional<JsonObject>{};
  }
  if (auto written = writeFileAtomically(path, *text); !written) {
    spdlog::warn("Failed to load {}: {}", filename.toStdString(),
                 written.error().message.toStdString());
    return std::optional<JsonObject>{};
  }
  return std::optional<JsonObject>(std::move(raw));
}

Result<void> ConfigService::migrate(JsonObject& data,
                                    std::int64_t current) const {
  if (current > kCurrentConfigSchemaVersion) {
    return std::unexpected(
        Error{QStringLiteral("Unsupported JSON schema version %1; latest is %2")
                  .arg(current)
                  .arg(kCurrentConfigSchemaVersion)});
  }
  for (std::int64_t target = current + 1; target <= kCurrentConfigSchemaVersion;
       ++target) {
    const auto migration = m_migrations.find(static_cast<int>(target));
    if (migration == m_migrations.end()) {
      return std::unexpected(Error{
          QStringLiteral("Missing JSON migration step for target version %1")
              .arg(target)});
    }
    migration->second(data);
  }
  return {};
}

QString configDir() {
  const QString base = qEnvironmentVariable(
      "XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config"));
  return QDir(base).filePath(QStringLiteral("pxmodrim"));
}

QString communityRulesFile() {
  return QDir(configDir()).filePath(QStringLiteral("communityRules.json"));
}

std::optional<QString> readGameVersion(const QString& gamePath) {
  const QString path = QDir(gamePath).filePath(QStringLiteral("Version.txt"));
  if (!QFileInfo::exists(path)) {
    return std::nullopt;
  }
  auto bytes = readFileBytes(path);
  if (!bytes) {
    spdlog::warn("Failed to read game version from {}", path.toStdString());
    return std::nullopt;
  }
  return QString::fromUtf8(bytes->data(), static_cast<qsizetype>(bytes->size()))
      .trimmed();
}

PathConfig detectGamePaths() {
  QString foundRoot;
  const GamePaths found = findGamePaths(foundRoot);

  PathConfig result;
  result.game = found.game.toStdString();
  result.local = found.local.toStdString();
  result.workshop = found.workshop.toStdString();
  result.config_folder = detectConfigFolder(foundRoot).toStdString();
  result.community_rules_file = communityRulesFile().toStdString();
  return result;
}

AppConfigService::AppConfigService(ConfigService& svc) : m_svc(svc) {}

QCoro::Task<Result<void>> AppConfigService::setup() {
  auto loaded = m_svc.load<AppConfig>(kConfigFile);
  if (!loaded) {
    co_return std::unexpected(loaded.error());
  }
  m_cfg = std::move(*loaded);
  co_return {};
}

const AppConfig& AppConfigService::cfg() const {
  Q_ASSERT(m_cfg.has_value());
  return *m_cfg;
}

Result<void> AppConfigService::saveCfg() const {
  if (!m_cfg) {
    return {};
  }
  return m_svc.save(kConfigFile, *m_cfg);
}

}  // namespace px::modrim
