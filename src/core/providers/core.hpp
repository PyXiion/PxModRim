#pragma once

#include "core/providers/base.hpp"

namespace px::modrim::providers {

// Base-game and DLC mods under the game's `Data/` directory.
class CoreModProvider final : public BaseModProvider {
public:
    // `gamePath` is the RimWorld install root; `Data/` is resolved inside it.
    explicit CoreModProvider(QString gamePath, MetadataCacheStore* metadataCache = nullptr);

    QCoro::Task<Result<ModMap>> discover(QString target_version, Profiler* timer = nullptr,
                                         MetadataCacheStore* metadata_cache = nullptr,
                                         bool force_reparse = false) override;
};

}  // namespace px::modrim::providers
