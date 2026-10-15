#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <expected>
#include <string>
#include <variant>
#include <vector>

#include <glaze/glaze.hpp>
#include <QHash>
#include <QString>
#include <QStringList>

namespace px::core::workshop {

// Catalog responses are strict: every key is present (nullable ones may be null), unknown keys are
// ignored and a value of the wrong type fails. Writing emits nulls so payloads round-trip exactly.
inline constexpr glz::opts kWireOpts{
    .error_on_unknown_keys = false,
    .skip_null_members = false,
    .error_on_missing_keys = true,
};

// Literal-typed wire fields are enums so that a value outside the catalog's contract fails to decode.
enum class PreviewType { image, video };
enum class ModSource { steam };
enum class ModDescriptionFormat { bbcode };
enum class ModKind { mod };
enum class CollectionSource { steam, picked };
enum class CollectionDescriptionFormat { bbcode, text };
enum class CollectionKind { collection };

struct Author {
    std::optional<std::string> id;
    std::optional<std::string> name;
    std::optional<std::string> profile_url;

    bool operator==(const Author&) const = default;
};

struct Preview {
    PreviewType type = PreviewType::image;
    std::string url;

    bool operator==(const Preview&) const = default;
};

struct Votes {
    std::int64_t up = 0;
    std::int64_t down = 0;
    std::optional<double> positive_percent;

    bool operator==(const Votes&) const = default;
};

struct CatalogMod {
    std::string id;
    ModSource source = ModSource::steam;
    std::string title;
    Author author;
    std::string description;
    ModDescriptionFormat description_format = ModDescriptionFormat::bbcode;
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
    ModKind kind = ModKind::mod;

    bool operator==(const CatalogMod&) const = default;
};

struct CatalogCollection {
    std::string id;
    CollectionSource source = CollectionSource::steam;
    std::optional<std::string> steam_id;
    std::string title;
    Author author;
    std::string description;
    CollectionDescriptionFormat description_format = CollectionDescriptionFormat::text;
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
    CollectionKind kind = CollectionKind::collection;

    bool operator==(const CatalogCollection&) const = default;
};

// A mod or a collection, told apart by the wire `kind` tag.
using CatalogEntry = std::variant<CatalogMod, CatalogCollection>;

template <class T>
struct CatalogPage {
    std::vector<T> items;
    std::int64_t total = 0;
    std::optional<std::string> next_cursor;

    bool operator==(const CatalogPage&) const = default;
};

struct CollectionDetail {
    CatalogCollection collection;
    std::vector<CatalogEntry> members;
    std::vector<std::string> unavailable_ids;
    bool is_complete = false;

    bool operator==(const CollectionDetail&) const = default;
};

struct Discover {
    CatalogPage<CatalogMod> mods;
    CatalogPage<CatalogCollection> collections;

    bool operator==(const Discover&) const = default;
};

struct ResolvedDownload {
    std::vector<std::string> roots;
    std::map<std::string, CatalogEntry> items;
    std::vector<std::string> mod_ids;
    std::vector<std::string> unavailable_ids;
    std::vector<std::string> incomplete_collection_ids;
    bool is_complete = false;

    bool operator==(const ResolvedDownload&) const = default;
};

struct CatalogQuery {
    QString query;
    std::optional<QString> tag;
    std::optional<QString> version;
    QString sort = QStringLiteral("popular");
    int limit = 24;
    std::optional<QString> cursor;
    QString source = QStringLiteral("all");
};

enum class InstallState { missing, installed, outdated };

// What a download would fetch, in dependency order. Built on the client side, so it uses Qt types.
struct DownloadPlan {
    QStringList to_download{};
    QStringList already_current{};
    QStringList unavailable_ids{};
    QStringList incomplete_collection_ids{};
    QHash<QString, QString> titles{};
    bool is_complete = false;
};

// Failure of a catalog request. `status` is the HTTP status when the server answered, empty when the
// request never got a response (disabled catalog, unreachable host, undecodable payload).
struct CatalogError {
    QString message;
    std::optional<int> status;
};

template <class T>
using CatalogResult = std::expected<T, CatalogError>;

}  // namespace px::core::workshop

template <>
struct glz::meta<px::core::workshop::PreviewType> {
    using enum px::core::workshop::PreviewType;
    static constexpr auto value = glz::enumerate(image, video);
};

template <>
struct glz::meta<px::core::workshop::ModSource> {
    using enum px::core::workshop::ModSource;
    static constexpr auto value = glz::enumerate(steam);
};

template <>
struct glz::meta<px::core::workshop::ModDescriptionFormat> {
    using enum px::core::workshop::ModDescriptionFormat;
    static constexpr auto value = glz::enumerate(bbcode);
};

template <>
struct glz::meta<px::core::workshop::ModKind> {
    using enum px::core::workshop::ModKind;
    static constexpr auto value = glz::enumerate(mod);
};

template <>
struct glz::meta<px::core::workshop::CollectionSource> {
    using enum px::core::workshop::CollectionSource;
    static constexpr auto value = glz::enumerate(steam, picked);
};

template <>
struct glz::meta<px::core::workshop::CollectionDescriptionFormat> {
    using enum px::core::workshop::CollectionDescriptionFormat;
    static constexpr auto value = glz::enumerate(bbcode, text);
};

template <>
struct glz::meta<px::core::workshop::CollectionKind> {
    using enum px::core::workshop::CollectionKind;
    static constexpr auto value = glz::enumerate(collection);
};
