#pragma once

#include "core/providers/base.hpp"

namespace px::modrim::providers {

// Base-game and DLC mods under the game's `Data/` directory.
class CoreModProvider final : public BaseModProvider {
 public:
  // `gamePath` is the RimWorld install root; `Data/` is resolved inside it.
  explicit CoreModProvider(QString gamePath,
                           MetadataCacheStore* metadataCache = nullptr);

  QCoro::Task<Result<ModMap>> discover(
      QString targetVersion, Profiler* timer = nullptr,
      MetadataCacheStore* metadataCache = nullptr,
      bool forceReparse = false) override;
};

}  // namespace px::modrim::providers
