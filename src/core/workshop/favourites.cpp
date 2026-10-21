#include "core/workshop/favourites.hpp"

#include <algorithm>
#include <utility>

namespace px::modrim::workshop {

FavouriteCollections::FavouriteCollections(ConfigService& service)
    : m_config(service, QStringLiteral("workshop_favourites")) {}

bool FavouriteCollections::contains(const std::string& collectionId) const {
  return std::ranges::any_of(
      m_config.value().collections,
      [&](const CatalogCollection& item) { return item.id == collectionId; });
}

void FavouriteCollections::set(const CatalogCollection& collection,
                               bool favourite) {
  if (contains(collection.id) == favourite) {
    return;
  }
  std::vector<CatalogCollection> rest;
  for (const CatalogCollection& stored : m_config.value().collections) {
    if (stored.id != collection.id) {
      rest.push_back(stored);
    }
  }
  if (favourite) {
    rest.insert(rest.begin(), collection);
  }
  save(std::move(rest));
}

void FavouriteCollections::remember(const CatalogCollection& collection) {
  if (!contains(collection.id)) {
    return;
  }
  const auto& stored = m_config.value().collections;
  if (std::ranges::find(stored, collection) != stored.end()) {
    return;
  }
  std::vector<CatalogCollection> updated = stored;
  for (CatalogCollection& entry : updated) {
    if (entry.id == collection.id) {
      entry = collection;
    }
  }
  save(std::move(updated));
}

void FavouriteCollections::save(std::vector<CatalogCollection> collections) {
  m_config.update(FavouritesState{std::move(collections)});
}

}  // namespace px::modrim::workshop
