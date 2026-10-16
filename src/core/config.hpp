#pragma once

#include <QFileInfo>
#include <QString>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <QCoro/QCoroTask>

#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include "core/constants.hpp"
#include "core/error.hpp"
#include "core/file_io.hpp"
#include "core/sort/config.hpp"

namespace px::modrim {

inline constexpr int kCurrentConfigSchemaVersion = 1;
inline constexpr std::string_view kSchemaVersionKey = "schema_version";

// Unknown keys are skipped so files written by newer or older builds still
// load.
inline constexpr glz::opts kJsonReadOpts{.error_on_unknown_keys = false};
inline constexpr glz::opts kJsonPrettyOpts{.prettify = true,
                                           .indentation_width = 2};

using JsonObject = glz::json_t::object_t;

// Rewrites a raw JSON object from schema version N-1 to N.
using JsonMigration = std::function<void(JsonObject&)>;
using JsonMigrations = std::map<int, JsonMigration>;

// Version 1 is the first versioned layout, so there is nothing to rewrite yet.
inline JsonMigrations defaultJsonMigrations() {
  return {{1, [](JsonObject&) {}}};
}

// glaze does not serialise QString, so serialised fields use std::string.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct PathConfig {
  std::string game;
  std::string local;
  std::string workshop;
  std::string config_folder;
  std::string community_rules_file;
  std::string no_version_warning_file;
  std::string use_this_instead_file;

  bool operator==(const PathConfig&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct AppConfig {
  PathConfig paths;
  sort::SortSettings sort;
  int max_snapshots = 10;
  bool compact_mod_list = false;
  std::string log_upload_endpoint = "https://paste.rs/";
  std::string launch_args;
  std::string launch_wrapper;
  AfterLaunch after_launch = AfterLaunch::kStay;
  bool launch_confirm_errors = true;
  bool launch_confirm_unsaved = true;
  bool launch_confirm_running = true;

  bool operator==(const AppConfig&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// File-based repository for typed JSON config files in one directory.
// Versioned files (config.json, ui_prefs.json) carry a schema_version key and
// are migrated and rewritten on load when older. A missing, unreadable, or
// corrupt file loads as the struct defaults, with a warning.
class ConfigService {
 public:
  explicit ConfigService(QString configDir,
                         JsonMigrations migrations = defaultJsonMigrations());

  const QString& directory() const noexcept;

  // Fails only when a versioned file cannot be migrated.
  template <class T>
  Result<T> load(const QString& filename) const;

  // Like `load`, but nullopt when the file does not exist yet.
  template <class T>
  Result<std::optional<T>> loadExisting(const QString& filename) const;

  template <class T>
  Result<void> save(const QString& filename, const T& data) const;

  // The undecoded top-level object of a file, or {} when it cannot be read.
  JsonObject loadRaw(const QString& filename) const;

 private:
  QString filePath(const QString& filename) const;
  static bool isVersioned(const QString& filename);
  // Appends the schema marker after the struct's own members, where the legacy
  // writer put it.
  static std::string withSchemaMarker(std::string prettyObject);
  Result<std::optional<JsonObject>> loadVersioned(
      const QString& filename) const;
  Result<void> migrate(JsonObject& data, std::int64_t current) const;

  QString m_dir;
  JsonMigrations m_migrations;
};

template <class T>
Result<T> ConfigService::load(const QString& filename) const {
  const QString path = filePath(filename);
  if (!QFileInfo::exists(path)) {
    return T{};
  }

  std::string text;
  if (isVersioned(filename)) {
    auto document = loadVersioned(filename);
    if (!document) {
      return std::unexpected(document.error());
    }
    if (!document->has_value()) {
      return T{};
    }
    auto encoded = glz::write_json(**document);
    if (!encoded) {
      return T{};
    }
    text = std::move(*encoded);
  } else {
    auto bytes = readFileBytes(path);
    if (!bytes) {
      return T{};
    }
    text = std::move(*bytes);
  }

  T value{};
  if (auto error = glz::read<kJsonReadOpts>(value, text)) {
    spdlog::warn("Failed to load {}: {}", filename.toStdString(),
                 glz::format_error(error, text));
    return T{};
  }
  return value;
}

template <class T>
Result<std::optional<T>> ConfigService::loadExisting(
    const QString& filename) const {
  if (!QFileInfo::exists(filePath(filename))) {
    return std::optional<T>{};
  }
  auto loaded = load<T>(filename);
  if (!loaded) {
    return std::unexpected(loaded.error());
  }
  return std::optional<T>(std::move(*loaded));
}

template <class T>
Result<void> ConfigService::save(const QString& filename, const T& data) const {
  auto text = glz::write<kJsonPrettyOpts>(data);
  if (!text) {
    return std::unexpected(
        Error{QStringLiteral("Cannot encode %1").arg(filename)});
  }
  if (isVersioned(filename)) {
    *text = withSchemaMarker(std::move(*text));
  }
  return writeFileAtomically(filePath(filename), *text);
}

QString configDir();
QString communityRulesFile();

// The game version string from Version.txt, or nullopt when it is missing or
// unreadable.
std::optional<QString> readGameVersion(const QString& gamePath);

PathConfig detectGamePaths();

// Owns the AppConfig lifecycle: load on setup, save on request.
class AppConfigService {
 public:
  explicit AppConfigService(ConfigService& svc);

  QCoro::Task<Result<void>> setup();

  // Valid only after a successful setup().
  const AppConfig& cfg() const;

  Result<void> saveCfg() const;

 private:
  ConfigService& m_svc;
  std::optional<AppConfig> m_cfg;
};

}  // namespace px::modrim
