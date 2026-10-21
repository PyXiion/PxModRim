#pragma once

#include <QThreadPool>
#include <memory>
#include <vector>

#include "core/config.hpp"
#include "core/providers/base.hpp"

namespace px::modrim::providers {

using ProviderList = std::vector<std::unique_ptr<BaseModProvider>>;

// The default providers for the configured paths: base game, local, downloaded
// and Workshop. Paths that are not set produce no provider. `pool` is not owned
// and must outlive the providers; null uses the global pool.
ProviderList createProviders(const PathConfig& paths,
                             QThreadPool* pool = nullptr);

}  // namespace px::modrim::providers
