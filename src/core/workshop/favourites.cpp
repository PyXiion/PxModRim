#include "core/workshop/favourites.h"

#include <algorithm>
#include <utility>

namespace px::core::workshop {

FavouriteCollections::FavouriteCollections(ConfigService* service)
    : config_(service, QStringLiteral("workshop_favourites")) {
    for (const CatalogCollection& collection : config_.value().collections) {
        ids_.insert(collection.id);
    }
}

bool FavouriteCollections::contains(const std::string& collection_id) const {
    return ids_.contains(collection_id);
}

void FavouriteCollections::set(const CatalogCollection& collection, bool favourite) {
    if (contains(collection.id) == favourite) {
        return;
    }
    std::vector<CatalogCollection> rest;
    for (const CatalogCollection& stored : config_.value().collections) {
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
    const auto& stored = config_.value().collections;
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
    ids_.clear();
    for (const CatalogCollection& collection : collections) {
        ids_.insert(collection.id);
    }
    config_.update(FavouritesState{std::move(collections)});
}

}  // namespace px::core::workshop
