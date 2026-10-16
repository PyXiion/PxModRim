#include <QTemporaryDir>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "core/workshop/favourites.hpp"
#include "core/workshop/types.hpp"
#include "support.hpp"

using namespace px::modrim;
using namespace px::modrim::workshop;
using workshop_test::decodeCollection;

namespace {

CatalogCollection makeCollection(std::string id, std::string title = "Pack") {
  CatalogCollection collection =
      decodeCollection(workshop_test::kCollectionJson);
  collection.id = std::move(id);
  collection.title = std::move(title);
  return collection;
}

}  // namespace

TEST_CASE("workshop favourites persist newest first and survive a restart") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  ConfigService service(dir.path());

  FavouriteCollections favourites(&service);
  const CatalogCollection first = makeCollection("steam:1");
  const CatalogCollection second = makeCollection("steam:2");
  favourites.set(first, true);
  favourites.set(second, true);
  // Setting an already-starred collection again is a no-op: it does not jump to
  // the front.
  favourites.set(second, true);

  FavouriteCollections reloaded(&service);
  REQUIRE(reloaded.items().size() == 2);
  CHECK(reloaded.items()[0].id == "steam:2");
  CHECK(reloaded.items()[1].id == "steam:1");
  CHECK(reloaded.contains("steam:1"));

  reloaded.set(first, false);
  FavouriteCollections afterRemoval(&service);
  REQUIRE(afterRemoval.items().size() == 1);
  CHECK(afterRemoval.items()[0].id == "steam:2");
  CHECK_FALSE(afterRemoval.contains("steam:1"));
}

TEST_CASE("workshop favourites remember refreshes only starred snapshots") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  ConfigService service(dir.path());

  FavouriteCollections favourites(&service);
  favourites.set(makeCollection("steam:1", "Old title"), true);

  int saves = 0;
  favourites.changed().connect([&](const FavouritesState&) { ++saves; });

  // An equal collection does not save.
  favourites.remember(makeCollection("steam:1", "Old title"));
  // A collection that was never starred does not save.
  favourites.remember(makeCollection("steam:2", "Not starred"));
  CHECK(saves == 0);

  favourites.remember(makeCollection("steam:1", "New title"));
  CHECK(saves == 1);

  FavouriteCollections reloaded(&service);
  REQUIRE(reloaded.items().size() == 1);
  CHECK(reloaded.items()[0].title == "New title");
  CHECK(reloaded.items()[0].id == "steam:1");
}
