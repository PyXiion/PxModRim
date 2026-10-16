#include <catch2/catch_test_macros.hpp>

#include <string>

#include <glaze/glaze.hpp>

#include "core/workshop/types.hpp"
#include "support.hpp"

using namespace px::modrim::workshop;
using workshop_test::decode_collection;
using workshop_test::decode_mod;
using workshop_test::wire;

namespace {

// Decodes `json` as a `T`, returning whether glaze accepted it.
template <class T>
bool accepts(const std::string& json) {
    T value{};
    return !glz::read<kWireOpts>(value, json);
}

}  // namespace

TEST_CASE("workshop mod decodes with nulls and round-trips through the wire form") {
    CatalogMod mod = decode_mod(workshop_test::kModJson);
    REQUIRE(mod.id == "2009463077");
    REQUIRE(mod.title == "Harmony");
    REQUIRE(mod.votes.has_value());
    REQUIRE(mod.votes->positive_percent == 90.0);
    REQUIRE(mod.previews[1].type == PreviewType::video);
    REQUIRE(mod.file_size == std::string("18446744073709551615"));

    // Absent values stay absent instead of being invented.
    const std::string nulled = R"({
      "id": "7", "kind": "mod", "source": "steam", "title": "T",
      "author": {"id": "1", "name": null, "profile_url": null},
      "description": "", "description_format": "bbcode", "preview_url": null,
      "previews": [], "workshop_url": "u", "tags": [], "supported_versions": [],
      "created_at": null, "updated_at": null, "file_size": null, "subscriptions": null,
      "votes": null, "dependencies": [], "package_id": null, "incompatible": false
    })";
    CatalogMod sparse = decode_mod(nulled);
    CHECK_FALSE(sparse.author.name.has_value());
    CHECK_FALSE(sparse.votes.has_value());
    CHECK_FALSE(sparse.updated_at.has_value());
    CHECK_FALSE(sparse.file_size.has_value());

    const std::string encoded = wire(sparse);
    CHECK(encoded.find(R"("kind":"mod")") != std::string::npos);
    CHECK(decode_mod(encoded) == sparse);
    CHECK(decode_mod(wire(mod)) == mod);
}

TEST_CASE("workshop steam collection and empty page decode") {
    CatalogCollection collection = decode_collection(R"({
      "id": "steam:123", "kind": "collection", "source": "steam", "steam_id": "123",
      "title": "Pack", "author": {"id": null, "name": "Curator", "profile_url": null},
      "description": "[b]x[/b]", "description_format": "bbcode",
      "preview_url": null, "previews": [],
      "workshop_url": "https://steamcommunity.com/sharedfiles/filedetails/?id=123",
      "tags": [], "supported_versions": ["1.6"], "created_at": null, "updated_at": 5,
      "member_ids": ["2009463077", "steam:456"], "member_count": 2,
      "member_previews": ["https://images.example.test/a.png"],
      "total_size": "10", "no_common_version": true
    })");
    CHECK(collection.member_ids == std::vector<std::string>{"2009463077", "steam:456"});
    CHECK(collection.source == CollectionSource::steam);
    CHECK(collection.no_common_version);
    CHECK(decode_collection(wire(collection)) == collection);

    CatalogPage<CatalogMod> page{};
    REQUIRE(accepts<CatalogPage<CatalogMod>>(R"({"items":[],"total":0,"next_cursor":null})"));
    REQUIRE_FALSE(glz::read<kWireOpts>(page, std::string(R"({"items":[],"total":0,"next_cursor":null})")));
    CHECK(page.items.empty());
    CHECK(page.total == 0);
    CHECK_FALSE(page.next_cursor.has_value());
}

TEST_CASE("workshop wire decoding rejects payloads outside the catalog contract") {
    // A numeric id is not a string.
    std::string numeric_id(workshop_test::kModJson);
    numeric_id.replace(numeric_id.find("\"2009463077\""), 12, "123");
    CHECK_FALSE(accepts<CatalogMod>(numeric_id));

    // A collection payload is not a mod.
    CHECK_FALSE(accepts<CatalogMod>(std::string(workshop_test::kCollectionJson)));

    // A mod payload is not a collection.
    CHECK_FALSE(accepts<CatalogCollection>(std::string(workshop_test::kModJson)));

    // A missing required key fails.
    std::string missing_title(workshop_test::kModJson);
    missing_title.replace(missing_title.find("\"title\": \"Harmony\","), 21, "");
    CHECK_FALSE(accepts<CatalogMod>(missing_title));

    // A preview type outside image|video fails.
    std::string bad_preview(workshop_test::kModJson);
    bad_preview.replace(bad_preview.find("\"image\""), 7, "\"audio\"");
    CHECK_FALSE(accepts<CatalogMod>(bad_preview));

    CHECK_FALSE(accepts<CatalogMod>("not-json"));
}

TEST_CASE("workshop wire decoding ignores keys the contract does not name") {
    std::string extra(workshop_test::kModJson);
    extra.insert(1, R"("unexpected": {"nested": true},)");
    CHECK(accepts<CatalogMod>(extra));
}

TEST_CASE("workshop catalog query has the documented defaults") {
    CatalogQuery query;
    CHECK(query.query.isEmpty());
    CHECK(query.sort == QStringLiteral("popular"));
    CHECK(query.limit == 24);
    CHECK(query.source == QStringLiteral("all"));
    CHECK_FALSE(query.tag.has_value());
    CHECK_FALSE(query.version.has_value());
    CHECK_FALSE(query.cursor.has_value());
}
