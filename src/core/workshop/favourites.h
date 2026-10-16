#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "core/config.h"
#include "core/events.h"
#include "core/plugin_config.h"
#include "core/workshop/types.h"

namespace px::modrim::workshop {

// The persisted form, plugins/workshop_favourites.json.
struct FavouritesState {
    std::vector<CatalogCollection> collections;

    bool operator==(const FavouritesState&) const = default;
};

// Collections the user starred, newest first. Each entry is the last catalog snapshot of the collection,
// so the list shows without a network round trip and survives the catalog dropping it.
class FavouriteCollections {
public:
    // Without a service the favourites live in memory only.
    explicit FavouriteCollections(ConfigService* service);

    Event<FavouritesState>& changed() noexcept { return config_.changed; }
    const std::vector<CatalogCollection>& items() const noexcept { return config_.value().collections; }
    bool contains(const std::string& collection_id) const;

    void set(const CatalogCollection& collection, bool favourite);
    // Refreshes the stored snapshot of a starred collection; other collections are ignored.
    void remember(const CatalogCollection& collection);

private:
    void save(std::vector<CatalogCollection> collections);

    PluginConfig<FavouritesState> config_;
    std::unordered_set<std::string> ids_;
};

}  // namespace px::modrim::workshop
