#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QStringList>

#include "core/config.h"
#include "core/providers/factory.h"
#include "core/providers/local.h"

using px::modrim::PathConfig;
using px::modrim::providers::create_providers;
using px::modrim::providers::SteamWorkshopModProvider;

TEST_CASE("no configured paths produce no providers") {
    CHECK(create_providers(PathConfig{}).empty());
}

TEST_CASE("each configured path yields its providers in core, local, downloaded, steam order") {
    PathConfig paths;
    paths.game = "/games/rimworld";
    paths.local = "/mods";
    paths.workshop = "/steam/workshop";

    const auto providers = create_providers(paths);
    QStringList ids;
    for (const auto& provider : providers) {
        ids.append(provider->provider_id());
    }
    CHECK(ids == QStringList{"core", "local", "downloaded", "steam"});
    CHECK(providers[0]->path() == QStringLiteral("/games/rimworld"));
    CHECK(providers[1]->path() == QStringLiteral("/mods"));
    CHECK(providers[2]->path() == QStringLiteral("/mods"));
    CHECK(providers[3]->path() == QStringLiteral("/steam/workshop"));
    CHECK(dynamic_cast<SteamWorkshopModProvider*>(providers[3].get()) != nullptr);
}

TEST_CASE("only the configured paths produce providers") {
    PathConfig paths;
    paths.local = "/mods";
    const auto providers = create_providers(paths);
    REQUIRE(providers.size() == 2);
    CHECK(providers[0]->provider_id() == QStringLiteral("local"));
    CHECK(providers[1]->provider_id() == QStringLiteral("downloaded"));
}
