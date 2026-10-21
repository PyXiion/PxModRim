#include <QString>
#include <QStringList>

#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "core/providers/factory.hpp"
#include "core/providers/folder.hpp"

using px::modrim::PathConfig;
using px::modrim::providers::createProviders;
using px::modrim::providers::FolderModProvider;

TEST_CASE("no configured paths produce no providers") {
  CHECK(createProviders(PathConfig{}).empty());
}

TEST_CASE(
    "each configured path yields its providers in core, local, downloaded, "
    "steam order") {
  PathConfig paths;
  paths.game = "/games/rimworld";
  paths.local = "/mods";
  paths.workshop = "/steam/workshop";

  const auto providers = createProviders(paths);
  QStringList ids;
  for (const auto& provider : providers) {
    ids.append(provider->providerId());
  }
  CHECK(ids == QStringList{"core", "local", "downloaded", "steam"});
  CHECK(providers[0]->path() == QStringLiteral("/games/rimworld"));
  CHECK(providers[1]->path() == QStringLiteral("/mods"));
  CHECK(providers[2]->path() == QStringLiteral("/mods"));
  CHECK(providers[3]->path() == QStringLiteral("/steam/workshop"));
  CHECK(dynamic_cast<FolderModProvider*>(providers[3].get()) != nullptr);
}

TEST_CASE("only the configured paths produce providers") {
  PathConfig paths;
  paths.local = "/mods";
  const auto providers = createProviders(paths);
  REQUIRE(providers.size() == 2);
  CHECK(providers[0]->providerId() == QStringLiteral("local"));
  CHECK(providers[1]->providerId() == QStringLiteral("downloaded"));
}
