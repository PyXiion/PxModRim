#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "core/config.hpp"
#include "core/events.hpp"
#include "core/plugin_config.hpp"
#include "core/workshop/types.hpp"

namespace px::modrim::workshop {

// The persisted form, plugins/workshop_favourites.json.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or on the wire
struct FavouritesState {
    std::vector<CatalogCollection> collections;

    bool operator==(const FavouritesState&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// Collections the user starred, newest first. Each entry is the last catalog snapshot of the collection,
// so the list shows without a network round trip and survives the catalog dropping it.
class FavouriteCollections {
public:
    // Without a service the favourites live in memory only.
    explicit FavouriteCollections(ConfigService* service);

    Event<FavouritesState>& changed() noexcept { return m_config.changed; }
    const std::vector<CatalogCollection>& items() const noexcept { return m_config.value().collections; }
    bool contains(const std::string& collectionId) const;

    void set(const CatalogCollection& collection, bool favourite);
    // Refreshes the stored snapshot of a starred collection; other collections are ignored.
    void remember(const CatalogCollection& collection);

private:
    void save(std::vector<CatalogCollection> collections);

    PluginConfig<FavouritesState> m_config;
    std::unordered_set<std::string> m_ids;
};

}  // namespace px::modrim::workshop
