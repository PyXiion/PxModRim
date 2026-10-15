#pragma once

#include <memory>
#include <vector>

#include <QThreadPool>

#include "core/config.h"
#include "core/providers/base.h"

namespace px::core::providers {

using ProviderList = std::vector<std::unique_ptr<BaseModProvider>>;

// The default providers for the configured paths: base game, local, downloaded and Workshop.
// Paths that are not set produce no provider. `pool` is not owned and must outlive the providers;
// null uses the global pool.
ProviderList create_providers(const PathConfig& paths, QThreadPool* pool = nullptr);

}  // namespace px::core::providers
