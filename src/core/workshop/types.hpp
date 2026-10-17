#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glaze/glaze.hpp>

namespace px::modrim::workshop {

// Catalog responses are strict: every key is present (nullable ones may be
// null), unknown keys are ignored and a value of the wrong type fails. Writing
// emits nulls so payloads round-trip exactly.
inline constexpr glz::opts kWireOpts{
    .error_on_unknown_keys = false,
    .skip_null_members = false,
    .error_on_missing_keys = true,
};

// Literal-typed wire fields are enums so that a value outside the catalog's
// contract fails to decode.
enum class PreviewType { kImage, kVideo };
enum class ModSource { kSteam };
enum class ModDescriptionFormat { kBbcode };
enum class ModKind { kMod };
enum class CollectionSource { kSteam, kPicked };
enum class CollectionDescriptionFormat { kBbcode, kText };
enum class CollectionKind { kCollection };

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct Author {
  std::optional<std::string> id;
  std::optional<std::string> name;
  std::optional<std::string> profile_url;

  bool operator==(const Author&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct Preview {
  PreviewType type = PreviewType::kImage;
  std::string url;

  bool operator==(const Preview&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct Votes {
  std::int64_t up = 0;
  std::int64_t down = 0;
  std::optional<double> positive_percent;

  bool operator==(const Votes&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct CatalogMod {
  std::string id;
  ModSource source = ModSource::kSteam;
  std::string title;
  Author author;
  std::string description;
  ModDescriptionFormat description_format = ModDescriptionFormat::kBbcode;
  std::optional<std::string> preview_url;
  std::vector<Preview> previews;
  std::string workshop_url;
  std::vector<std::string> tags;
  std::vector<std::string> supported_versions;
  std::optional<std::int64_t> created_at;
  std::optional<std::int64_t> updated_at;
  std::optional<std::string> file_size;
  std::optional<std::int64_t> subscriptions;
  std::optional<Votes> votes;
  std::vector<std::string> dependencies;
  std::optional<std::string> package_id;
  bool incompatible = false;
  // The wire tag: decoding a collection payload as a mod fails here.
  ModKind kind = ModKind::kMod;

  bool operator==(const CatalogMod&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct CatalogCollection {
  std::string id;
  CollectionSource source = CollectionSource::kSteam;
  std::optional<std::string> steam_id;
  std::string title;
  Author author;
  std::string description;
  CollectionDescriptionFormat description_format =
      CollectionDescriptionFormat::kText;
  std::optional<std::string> preview_url;
  std::vector<Preview> previews;
  std::optional<std::string> workshop_url;
  std::vector<std::string> tags;
  std::vector<std::string> supported_versions;
  std::optional<std::int64_t> created_at;
  std::optional<std::int64_t> updated_at;
  std::vector<std::string> member_ids;
  std::int64_t member_count = 0;
  std::vector<std::string> member_previews;
  std::optional<std::string> total_size;
  bool no_common_version = false;
  // The wire tag: decoding a mod payload as a collection fails here.
  CollectionKind kind = CollectionKind::kCollection;

  bool operator==(const CatalogCollection&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// A mod or a collection, told apart by the wire `kind` tag.
using CatalogEntry = std::variant<CatalogMod, CatalogCollection>;

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
template <class T>
struct CatalogPage {
  std::vector<T> items;
  std::int64_t total = 0;
  std::optional<std::string> next_cursor;

  bool operator==(const CatalogPage&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct CollectionDetail {
  CatalogCollection collection;
  std::vector<CatalogEntry> members;
  std::vector<std::string> unavailable_ids;
  bool is_complete = false;

  bool operator==(const CollectionDetail&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct Discover {
  CatalogPage<CatalogMod> mods;
  CatalogPage<CatalogCollection> collections;

  bool operator==(const Discover&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct ResolvedDownload {
  std::vector<std::string> roots;
  // Not std::map: libc++'s map operator<=> is not SFINAE-friendly, so glaze's
  // ordered<> probe hard-errors on a variant of unordered alternatives.
  std::unordered_map<std::string, CatalogEntry> items;
  std::vector<std::string> mod_ids;
  std::vector<std::string> unavailable_ids;
  std::vector<std::string> incomplete_collection_ids;
  bool is_complete = false;

  bool operator==(const ResolvedDownload&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct CatalogQuery {
  QString query;
  std::optional<QString> tag;
  std::optional<QString> version;
  QString sort = QStringLiteral("popular");
  int limit = 24;
  std::optional<QString> cursor;
  QString source = QStringLiteral("all");
};
// NOLINTEND(readability-identifier-naming)

enum class InstallState { kMissing, kInstalled, kOutdated };

// What a download would fetch, in dependency order. Built on the client side,
// so it uses Qt types. NOLINTBEGIN(readability-identifier-naming): fields are
// JSON keys on disk or on the wire
struct DownloadPlan {
  QStringList to_download{};
  QStringList already_current{};
  QStringList unavailable_ids{};
  QStringList incomplete_collection_ids{};
  QHash<QString, QString> titles{};
  bool is_complete = false;
};
// NOLINTEND(readability-identifier-naming)

// Failure of a catalog request. `status` is the HTTP status when the server
// answered, empty when the request never got a response (disabled catalog,
// unreachable host, undecodable payload).
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct CatalogError {
  QString message;
  std::optional<int> status;
};
// NOLINTEND(readability-identifier-naming)

template <class T>
using CatalogResult = std::expected<T, CatalogError>;

}  // namespace px::modrim::workshop

// The JSON strings are part of the catalog API, so they are spelled out instead
// of derived from the enumerator names.
// NOLINTBEGIN(readability-identifier-naming): glaze looks the member up by this
// exact name
template <>
struct glz::meta<px::modrim::workshop::PreviewType> {
  using enum px::modrim::workshop::PreviewType;
  static constexpr auto value =
      glz::enumerate("image", kImage, "video", kVideo);
};

template <>
struct glz::meta<px::modrim::workshop::ModSource> {
  using enum px::modrim::workshop::ModSource;
  static constexpr auto value = glz::enumerate("steam", kSteam);
};

template <>
struct glz::meta<px::modrim::workshop::ModDescriptionFormat> {
  using enum px::modrim::workshop::ModDescriptionFormat;
  static constexpr auto value = glz::enumerate("bbcode", kBbcode);
};

template <>
struct glz::meta<px::modrim::workshop::ModKind> {
  using enum px::modrim::workshop::ModKind;
  static constexpr auto value = glz::enumerate("mod", kMod);
};

template <>
struct glz::meta<px::modrim::workshop::CollectionSource> {
  using enum px::modrim::workshop::CollectionSource;
  static constexpr auto value =
      glz::enumerate("steam", kSteam, "picked", kPicked);
};

template <>
struct glz::meta<px::modrim::workshop::CollectionDescriptionFormat> {
  using enum px::modrim::workshop::CollectionDescriptionFormat;
  static constexpr auto value =
      glz::enumerate("bbcode", kBbcode, "text", kText);
};

template <>
struct glz::meta<px::modrim::workshop::CollectionKind> {
  using enum px::modrim::workshop::CollectionKind;
  static constexpr auto value = glz::enumerate("collection", kCollection);
};
// NOLINTEND(readability-identifier-naming)
