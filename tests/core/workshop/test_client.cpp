#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

#include <glaze/glaze.hpp>
#include <QString>
#include <QUrlQuery>

#include "../test_support.hpp"
#include "core/workshop/client.hpp"
#include "core/workshop/types.hpp"
#include "support.hpp"

using namespace px::modrim::workshop;
using test_support::TestApplication;
using workshop_test::FakeCatalog;
using workshop_test::HttpRequest;
using workshop_test::HttpResponse;
using workshop_test::decodeCollection;
using workshop_test::decodeMod;
using workshop_test::run;
using workshop_test::unreachableUrl;
using workshop_test::wire;

namespace {

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on the wire
struct ResolveRequest {
    std::vector<std::string> ids;
    std::vector<std::string> collection_ids;
};
// NOLINTEND(readability-identifier-naming)

struct IdsRequest {
    std::vector<std::string> ids;
};

// The `{"items":[...],"total":N,"next_cursor":...}` page shape.
std::string page(const std::string& items, int total, const std::string& cursorJson) {
    return R"({"items":[)" + items + R"(],"total":)" + std::to_string(total) + R"(,"next_cursor":)" + cursorJson + "}";
}

std::string errorBody(const std::string& message) {
    return R"({"error":{"message":")" + message + R"("}})";
}

QString queryValue(const HttpRequest& request, const char* key) {
    return request.query().queryItemValue(QString::fromLatin1(key));
}

CatalogMod withId(std::string id) {
    CatalogMod mod = decodeMod(workshop_test::kModJson);
    mod.id = std::move(id);
    return mod;
}

CatalogCollection steamCollection(std::string id, std::string steamId, std::vector<std::string> members) {
    CatalogCollection collection = decodeCollection(workshop_test::kCollectionJson);
    collection.id = std::move(id);
    collection.source = CollectionSource::kSteam;
    collection.steam_id = std::move(steamId);
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
    const std::string nested = wire(steamCollection("steam:456", "456", {"2009463077"}));
    const std::string steam = wire(steamCollection("steam:123", "123", {"2009463077", "steam:456", "99"}));

    const std::string detailPicked = R"({"collection":)" + collection + R"(,"members":[)" + mod +
                                      R"(],"unavailable_ids":["99"],"is_complete":false})";
    const std::string detailSteam = R"({"collection":)" + steam + R"(,"members":[)" + mod + "," + nested +
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
            return {200, detailPicked};
        }
        if (path == "/catalog/collections/steam/123") {
            return {200, detailSteam};
        }
        if (path == "/catalog/resolve") {
            return {200, resolution};
        }
        return {404, errorBody("unknown")};
    });

    CatalogClient client(server.url());
    const auto discover = run(client.discover());
    REQUIRE(discover.has_value());
    CHECK(discover->mods.items[0].file_size == std::string("18446744073709551615"));
    CHECK(discover->mods.items[0].votes->positive_percent == 90.0);
    CHECK(discover->mods.items[0].previews[1].type == PreviewType::kVideo);
    CHECK(discover->collections.items[0].description_format == CollectionDescriptionFormat::kText);

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
    CHECK(queryValue(sent, "q") == QStringLiteral("quality"));
    CHECK(queryValue(sent, "tag") == QStringLiteral("Libraries"));
    CHECK(queryValue(sent, "version") == QStringLiteral("1.6"));
    CHECK(queryValue(sent, "sort") == QStringLiteral("updated"));
    CHECK(queryValue(sent, "limit") == QStringLiteral("50"));
    CHECK(queryValue(sent, "cursor") == QStringLiteral("next"));
    CHECK(queryValue(sent, "source") == QStringLiteral("picked"));
    CHECK(sent.query().queryItems().size() == 7);

    const auto collections = run(client.collections(query));
    REQUIRE(collections.has_value());
    CHECK(collections->items[0].id == "picked:starter");

    const auto single = run(client.mod("2009463077"));
    REQUIRE(single.has_value());
    REQUIRE(single->has_value());
    CHECK((*single)->description == decodeMod(mod).description);
    CHECK((*single)->kind == ModKind::kMod);

    for (const char* id : {"picked:starter", "steam:123"}) {
        const auto detail = run(client.collection(id));
        REQUIRE(detail.has_value());
        REQUIRE(detail->has_value());
        const CollectionDetail& value = **detail;
        const bool isSteam = std::string(id) == "steam:123";
        REQUIRE(value.members.size() == (isSteam ? 2 : 1));
        CHECK(std::holds_alternative<CatalogMod>(value.members[0]));
        CHECK(value.unavailable_ids == std::vector<std::string>{"99"});
        CHECK_FALSE(value.is_complete);
        if (isSteam) {
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
        for (bool jsonBody : {true, false}) {
            FakeCatalog server([&](const HttpRequest&) -> HttpResponse {
                return jsonBody ? HttpResponse{status, errorBody("Catalog unavailable")}
                                 : HttpResponse{status, "<html>Upstream failure</html>"};
            });
            CatalogClient client(server.url());
            const auto result = run(client.mod("123"));
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().status == status);
            if (jsonBody) {
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
    for (bool jsonBody : {true, false}) {
        FakeCatalog server([&](const HttpRequest&) -> HttpResponse {
            return jsonBody ? HttpResponse{404, errorBody("Item unavailable")} : HttpResponse{404, "Not found"};
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
    CatalogClient failingClient(failing.url());
    const auto serverError = run(failingClient.mod("123"));
    REQUIRE_FALSE(serverError.has_value());
    CHECK(serverError.error().status == 500);
    const auto collectionError = run(failingClient.collection("steam:123"));
    REQUIRE_FALSE(collectionError.has_value());
    CHECK(collectionError.error().status == 500);

    CatalogClient offline(unreachableUrl());
    const auto network = run(offline.mod("123"));
    REQUIRE_FALSE(network.has_value());
    CHECK_FALSE(network.error().status.has_value());

    FakeCatalog garbage([](const HttpRequest&) -> HttpResponse { return {200, "not-json"}; });
    CatalogClient garbageClient(garbage.url());
    const auto decoding = run(garbageClient.collection("steam:123"));
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
    const auto batch = run(client.modsBatch({"123"}));
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
    const std::string validMod(workshop_test::kModJson);
    std::string numericId = validMod;
    numericId.replace(numericId.find("\"2009463077\""), 12, "123");
    std::string badKind = validMod;
    badKind.replace(badKind.find("\"mod\""), 5, "\"collection\"");
    std::string missingTitle = validMod;
    missingTitle.replace(missingTitle.find("\"title\": \"Harmony\","), 21, "");

    for (const std::string& body : {numericId, badKind, missingTitle, std::string("not-json")}) {
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
            items += (items.empty() ? "" : ",") + wire(withId(id));
        }
        std::string unavailableJson = "[";
        for (const std::string& id : unavailable) {
            unavailableJson += "\"" + id + "\"";
        }
        return {200, R"({"items":[)" + items + R"(],"unavailable_ids":)" + unavailableJson + "]}"};
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
    const auto result = run(client.modsBatch(qids));
    REQUIRE(result.has_value());

    REQUIRE(server.requests.size() == 3);
    CHECK(glz::read_json<IdsRequest>(server.requests[0].body)->ids.size() == 100);
    CHECK(glz::read_json<IdsRequest>(server.requests[1].body)->ids.size() == 100);
    CHECK(glz::read_json<IdsRequest>(server.requests[2].body)->ids.size() == 5);
    CHECK(result->items.size() == 204);
    CHECK(result->items.front().id == "1");
    CHECK(result->unavailableIds == std::vector<std::string>{"105"});

    const auto empty = run(client.modsBatch({}));
    REQUIRE(empty.has_value());
    CHECK(empty->items.empty());
    CHECK(empty->unavailableIds.empty());
    CHECK(server.requests.size() == 3);
}

TEST_CASE("workshop transport failures are catalog errors without a status") {
    TestApplication app;
    CatalogClient client(unreachableUrl());
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
        std::vector<std::string> modIds{"999"};
        modIds.insert(modIds.end(), body->ids.begin(), body->ids.end());
        std::string items = "{";
        for (const std::string& id : modIds) {
            items += "\"" + id + "\":" + wire(withId(id));
            items += id == modIds.back() ? "" : ",";
        }
        items += "}";
        std::string modsJson = "[";
        for (const std::string& id : modIds) {
            modsJson += "\"" + id + "\",";
        }
        modsJson.back() = ']';
        std::string rootsJson = "[";
        for (const std::string& id : body->ids) {
            rootsJson += "\"" + id + "\",";
        }
        for (const std::string& id : body->collection_ids) {
            rootsJson += "\"" + id + "\",";
        }
        rootsJson.back() = ']';

        return {200, R"({"roots":)" + rootsJson + R"(,"items":)" + items + R"(,"mod_ids":)" + modsJson +
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
