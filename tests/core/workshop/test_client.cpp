#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

#include <glaze/glaze.hpp>
#include <QString>
#include <QUrlQuery>

#include "../test_support.h"
#include "core/workshop/client.h"
#include "core/workshop/types.h"
#include "support.h"

using namespace px::modrim::workshop;
using test_support::TestApplication;
using workshop_test::FakeCatalog;
using workshop_test::HttpRequest;
using workshop_test::HttpResponse;
using workshop_test::decode_collection;
using workshop_test::decode_mod;
using workshop_test::run;
using workshop_test::unreachable_url;
using workshop_test::wire;

namespace {

struct ResolveRequest {
    std::vector<std::string> ids;
    std::vector<std::string> collection_ids;
};

struct IdsRequest {
    std::vector<std::string> ids;
};

// The `{"items":[...],"total":N,"next_cursor":...}` page shape.
std::string page(const std::string& items, int total, const std::string& cursor_json) {
    return R"({"items":[)" + items + R"(],"total":)" + std::to_string(total) + R"(,"next_cursor":)" + cursor_json + "}";
}

std::string error_body(const std::string& message) {
    return R"({"error":{"message":")" + message + R"("}})";
}

QString query_value(const HttpRequest& request, const char* key) {
    return request.query().queryItemValue(QString::fromLatin1(key));
}

CatalogMod with_id(std::string id) {
    CatalogMod mod = decode_mod(workshop_test::kModJson);
    mod.id = std::move(id);
    return mod;
}

CatalogCollection steam_collection(std::string id, std::string steam_id, std::vector<std::string> members) {
    CatalogCollection collection = decode_collection(workshop_test::kCollectionJson);
    collection.id = std::move(id);
    collection.source = CollectionSource::steam;
    collection.steam_id = std::move(steam_id);
    collection.workshop_url = "https://steamcommunity.com/sharedfiles/filedetails/?id=" + *collection.steam_id;
    collection.member_count = static_cast<std::int64_t>(members.size());
    collection.member_ids = std::move(members);
    return collection;
}

}  // namespace

TEST_CASE("workshop client decodes every endpoint and sends the query") {
    TestApplication app;
    const std::string mod = std::string(workshop_test::kModJson);
    const std::string collection = std::string(workshop_test::kCollectionJson);
    const std::string nested = wire(steam_collection("steam:456", "456", {"2009463077"}));
    const std::string steam = wire(steam_collection("steam:123", "123", {"2009463077", "steam:456", "99"}));

    const std::string detail_picked = R"({"collection":)" + collection + R"(,"members":[)" + mod +
                                      R"(],"unavailable_ids":["99"],"is_complete":false})";
    const std::string detail_steam = R"({"collection":)" + steam + R"(,"members":[)" + mod + "," + nested +
                                     R"(],"unavailable_ids":["99"],"is_complete":false})";
    const std::string resolution = R"({"roots":["picked:starter"],"items":{"2009463077":)" + mod +
                                   R"(,"picked:starter":)" + collection +
                                   R"(},"mod_ids":["2009463077"],"unavailable_ids":["99"],)"
                                   R"("incomplete_collection_ids":["picked:starter"],"is_complete":false})";

    FakeCatalog server([&](const HttpRequest& request) -> HttpResponse {
        const std::string path = request.path().toStdString();
        if (path == "/catalog/discover") {
            return {200, R"({"mods":)" + page(mod, 1, R"("next-page")") + R"(,"collections":)" +
                             page(collection, 1, "null") + "}"};
        }
        if (path == "/catalog/mods") {
            return {200, page(mod, 1, R"("next-page")")};
        }
        if (path == "/catalog/collections") {
            return {200, page(collection, 1, "null")};
        }
        if (path == "/catalog/mods/2009463077") {
            return {200, mod};
        }
        if (path == "/catalog/collections/picked/starter") {
            return {200, detail_picked};
        }
        if (path == "/catalog/collections/steam/123") {
            return {200, detail_steam};
        }
        if (path == "/catalog/resolve") {
            return {200, resolution};
        }
        return {404, error_body("unknown")};
    });

    CatalogClient client(server.url());
    const auto discover = run(client.discover());
    REQUIRE(discover.has_value());
    CHECK(discover->mods.items[0].file_size == std::string("18446744073709551615"));
    CHECK(discover->mods.items[0].votes->positive_percent == 90.0);
    CHECK(discover->mods.items[0].previews[1].type == PreviewType::video);
    CHECK(discover->collections.items[0].description_format == CollectionDescriptionFormat::text);

    CatalogQuery query;
    query.query = QStringLiteral("quality");
    query.tag = QStringLiteral("Libraries");
    query.version = QStringLiteral("1.6");
    query.sort = QStringLiteral("updated");
    query.limit = 50;
    query.cursor = QStringLiteral("next");
    query.source = QStringLiteral("picked");
    const auto mods = run(client.mods(query));
    REQUIRE(mods.has_value());
    CHECK(mods->next_cursor == std::optional<std::string>("next-page"));
    const HttpRequest& sent = server.requests.back();
    CHECK(query_value(sent, "q") == QStringLiteral("quality"));
    CHECK(query_value(sent, "tag") == QStringLiteral("Libraries"));
    CHECK(query_value(sent, "version") == QStringLiteral("1.6"));
    CHECK(query_value(sent, "sort") == QStringLiteral("updated"));
    CHECK(query_value(sent, "limit") == QStringLiteral("50"));
    CHECK(query_value(sent, "cursor") == QStringLiteral("next"));
    CHECK(query_value(sent, "source") == QStringLiteral("picked"));
    CHECK(sent.query().queryItems().size() == 7);

    const auto collections = run(client.collections(query));
    REQUIRE(collections.has_value());
    CHECK(collections->items[0].id == "picked:starter");

    const auto single = run(client.mod("2009463077"));
    REQUIRE(single.has_value());
    REQUIRE(single->has_value());
    CHECK((*single)->description == decode_mod(mod).description);
    CHECK((*single)->kind == ModKind::mod);

    for (const char* id : {"picked:starter", "steam:123"}) {
        const auto detail = run(client.collection(id));
        REQUIRE(detail.has_value());
        REQUIRE(detail->has_value());
        const CollectionDetail& value = **detail;
        const bool is_steam = std::string(id) == "steam:123";
        REQUIRE(value.members.size() == (is_steam ? 2 : 1));
        CHECK(std::holds_alternative<CatalogMod>(value.members[0]));
        CHECK(value.unavailable_ids == std::vector<std::string>{"99"});
        CHECK_FALSE(value.is_complete);
        if (is_steam) {
            CHECK(std::holds_alternative<CatalogCollection>(value.members[1]));
        }
    }

    const auto resolved = run(client.resolve({}, {"picked:starter"}));
    REQUIRE(resolved.has_value());
    CHECK(resolved->roots == std::vector<std::string>{"picked:starter"});
    CHECK(resolved->mod_ids == std::vector<std::string>{"2009463077"});
    CHECK(std::holds_alternative<CatalogCollection>(resolved->items.at("picked:starter")));
    CHECK(resolved->incomplete_collection_ids == std::vector<std::string>{"picked:starter"});
    CHECK_FALSE(resolved->is_complete);
    const auto body = glz::read_json<ResolveRequest>(server.requests.back().body);
    REQUIRE(body.has_value());
    CHECK(body->ids.empty());
    CHECK(body->collection_ids == std::vector<std::string>{"picked:starter"});
}

TEST_CASE("workshop client reports error statuses as errors, never as empty results") {
    TestApplication app;
    for (int status : {400, 401, 500, 502, 503}) {
        for (bool json_body : {true, false}) {
            FakeCatalog server([&](const HttpRequest&) -> HttpResponse {
                return json_body ? HttpResponse{status, error_body("Catalog unavailable")}
                                 : HttpResponse{status, "<html>Upstream failure</html>"};
            });
            CatalogClient client(server.url());
            const auto result = run(client.mod("123"));
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().status == status);
            if (json_body) {
                CHECK(result.error().message == QStringLiteral("Catalog unavailable"));
            } else {
                CHECK(result.error().message.contains(QStringLiteral("HTTP %1").arg(status)));
                CHECK_FALSE(result.error().message.contains(QStringLiteral("<html>")));
            }
        }
    }
}

TEST_CASE("workshop optional details return nothing only for a 404") {
    TestApplication app;
    for (bool json_body : {true, false}) {
        FakeCatalog server([&](const HttpRequest&) -> HttpResponse {
            return json_body ? HttpResponse{404, error_body("Item unavailable")} : HttpResponse{404, "Not found"};
        });
        CatalogClient client(server.url());
        const auto mod = run(client.mod("123"));
        REQUIRE(mod.has_value());
        CHECK_FALSE(mod->has_value());
        const auto collection = run(client.collection("steam:123"));
        REQUIRE(collection.has_value());
        CHECK_FALSE(collection->has_value());
    }
}

TEST_CASE("workshop optional details keep server, network and decoding failures") {
    TestApplication app;
    FakeCatalog failing([](const HttpRequest&) -> HttpResponse { return {500, "Server error"}; });
    CatalogClient failing_client(failing.url());
    const auto server_error = run(failing_client.mod("123"));
    REQUIRE_FALSE(server_error.has_value());
    CHECK(server_error.error().status == 500);
    const auto collection_error = run(failing_client.collection("steam:123"));
    REQUIRE_FALSE(collection_error.has_value());
    CHECK(collection_error.error().status == 500);

    CatalogClient offline(unreachable_url());
    const auto network = run(offline.mod("123"));
    REQUIRE_FALSE(network.has_value());
    CHECK_FALSE(network.error().status.has_value());

    FakeCatalog garbage([](const HttpRequest&) -> HttpResponse { return {200, "not-json"}; });
    CatalogClient garbage_client(garbage.url());
    const auto decoding = run(garbage_client.collection("steam:123"));
    REQUIRE_FALSE(decoding.has_value());
    CHECK(decoding.error().status == 200);
}

TEST_CASE("workshop non-optional endpoints keep 404 errors") {
    TestApplication app;
    FakeCatalog server([](const HttpRequest&) -> HttpResponse { return {404, ""}; });
    CatalogClient client(server.url());
    CatalogQuery query;

    const auto mods = run(client.mods(query));
    REQUIRE_FALSE(mods.has_value());
    CHECK(mods.error().status == 404);
    const auto collections = run(client.collections(query));
    REQUIRE_FALSE(collections.has_value());
    CHECK(collections.error().status == 404);
    const auto batch = run(client.mods_batch({"123"}));
    REQUIRE_FALSE(batch.has_value());
    CHECK(batch.error().status == 404);
    const auto resolved = run(client.resolve({"123"}, {}));
    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().status == 404);
}

TEST_CASE("workshop client rejects invalid collection ids before sending") {
    TestApplication app;
    FakeCatalog server([](const HttpRequest&) -> HttpResponse { return {200, "{}"}; });
    CatalogClient client(server.url());
    for (const char* id : {"steam", "steam:", ":123", "other:123"}) {
        const auto result = run(client.collection(id));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message == QStringLiteral("Invalid Workshop collection ID."));
    }
    CHECK(server.requests.empty());
}

TEST_CASE("workshop client treats responses that violate the wire contract as invalid") {
    TestApplication app;
    const std::string valid_mod(workshop_test::kModJson);
    std::string numeric_id = valid_mod;
    numeric_id.replace(numeric_id.find("\"2009463077\""), 12, "123");
    std::string bad_kind = valid_mod;
    bad_kind.replace(bad_kind.find("\"mod\""), 5, "\"collection\"");
    std::string missing_title = valid_mod;
    missing_title.replace(missing_title.find("\"title\": \"Harmony\","), 21, "");

    for (const std::string& body : {numeric_id, bad_kind, missing_title, std::string("not-json")}) {
        FakeCatalog server([&](const HttpRequest&) -> HttpResponse { return {200, body}; });
        CatalogClient client(server.url());
        const auto result = run(client.mod("123"));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message == QStringLiteral("The Workshop catalog returned an invalid response."));
        CHECK(result.error().status == 200);
    }
}

TEST_CASE("workshop batch requests split ids into chunks of 100") {
    TestApplication app;
    FakeCatalog server([](const HttpRequest& request) -> HttpResponse {
        const auto ids = glz::read_json<IdsRequest>(request.body);
        REQUIRE(ids.has_value());
        std::string items;
        std::vector<std::string> unavailable;
        for (const std::string& id : ids->ids) {
            if (id == "105") {
                unavailable.push_back(id);
                continue;
            }
            items += (items.empty() ? "" : ",") + wire(with_id(id));
        }
        std::string unavailable_json = "[";
        for (const std::string& id : unavailable) {
            unavailable_json += "\"" + id + "\"";
        }
        return {200, R"({"items":[)" + items + R"(],"unavailable_ids":)" + unavailable_json + "]}"};
    });

    CatalogClient client(server.url());
    std::vector<std::string> ids;
    for (int id = 1; id <= 205; ++id) {
        ids.push_back(std::to_string(id));
    }
    QStringList qids;
    for (const std::string& id : ids) {
        qids.append(QString::fromStdString(id));
    }
    const auto result = run(client.mods_batch(qids));
    REQUIRE(result.has_value());

    REQUIRE(server.requests.size() == 3);
    CHECK(glz::read_json<IdsRequest>(server.requests[0].body)->ids.size() == 100);
    CHECK(glz::read_json<IdsRequest>(server.requests[1].body)->ids.size() == 100);
    CHECK(glz::read_json<IdsRequest>(server.requests[2].body)->ids.size() == 5);
    CHECK(result->items.size() == 204);
    CHECK(result->items.front().id == "1");
    CHECK(result->unavailable_ids == std::vector<std::string>{"105"});

    const auto empty = run(client.mods_batch({}));
    REQUIRE(empty.has_value());
    CHECK(empty->items.empty());
    CHECK(empty->unavailable_ids.empty());
    CHECK(server.requests.size() == 3);
}

TEST_CASE("workshop transport failures are catalog errors without a status") {
    TestApplication app;
    CatalogClient client(unreachable_url());
    const auto result = run(client.discover());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.startsWith(QStringLiteral("Cannot reach the Workshop catalog")));
    CHECK_FALSE(result.error().status.has_value());
}

TEST_CASE("workshop disabled client sends nothing") {
    TestApplication app;
    CatalogClient client(QStringLiteral(""));
    const auto result = run(client.discover());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.contains(QStringLiteral("disabled")));
    CHECK_FALSE(result.error().status.has_value());
}

TEST_CASE("workshop resolve splits requests and merges their answers") {
    TestApplication app;
    std::vector<ResolveRequest> sent;
    FakeCatalog server([&](const HttpRequest& request) -> HttpResponse {
        const auto body = glz::read_json<ResolveRequest>(request.body);
        REQUIRE(body.has_value());
        sent.push_back(*body);
        const bool second = sent.size() == 2;

        // Every answer repeats the dependency 999, which must be reported once.
        std::vector<std::string> mod_ids{"999"};
        mod_ids.insert(mod_ids.end(), body->ids.begin(), body->ids.end());
        std::string items = "{";
        for (const std::string& id : mod_ids) {
            items += "\"" + id + "\":" + wire(with_id(id));
            items += id == mod_ids.back() ? "" : ",";
        }
        items += "}";
        std::string mods_json = "[";
        for (const std::string& id : mod_ids) {
            mods_json += "\"" + id + "\",";
        }
        mods_json.back() = ']';
        std::string roots_json = "[";
        for (const std::string& id : body->ids) {
            roots_json += "\"" + id + "\",";
        }
        for (const std::string& id : body->collection_ids) {
            roots_json += "\"" + id + "\",";
        }
        roots_json.back() = ']';

        return {200, R"({"roots":)" + roots_json + R"(,"items":)" + items + R"(,"mod_ids":)" + mods_json +
                         R"(,"unavailable_ids":)" + (second ? R"(["404"])" : "[]") +
                         R"(,"incomplete_collection_ids":)" + (second ? R"(["picked:pack-10"])" : "[]") +
                         R"(,"is_complete":)" + (second ? "false" : "true") + "}"};
    });

    std::vector<std::string> ids;
    for (int id = 1; id <= 103; ++id) {
        ids.push_back(std::to_string(id));
    }
    QStringList qids;
    for (const std::string& id : ids) {
        qids.append(QString::fromStdString(id));
    }
    QStringList collections;
    for (int index = 0; index < 11; ++index) {
        collections.append(QStringLiteral("picked:pack-%1").arg(index));
    }

    CatalogClient client(server.url());
    const auto resolved = run(client.resolve(qids, collections));
    REQUIRE(resolved.has_value());

    REQUIRE(sent.size() == 3);
    CHECK(sent[0].ids.size() == 50);
    CHECK(sent[1].ids.size() == 50);
    CHECK(sent[2].ids.size() == 3);
    CHECK(sent[0].collection_ids.size() == 10);
    CHECK(sent[1].collection_ids.size() == 1);
    CHECK(sent[2].collection_ids.empty());

    CHECK(resolved->roots.size() == 114);
    CHECK(resolved->mod_ids.size() == 104);
    CHECK(resolved->mod_ids.front() == "999");
    CHECK(resolved->unavailable_ids == std::vector<std::string>{"404"});
    CHECK(resolved->incomplete_collection_ids == std::vector<std::string>{"picked:pack-10"});
    CHECK_FALSE(resolved->is_complete);
}
