#include "core/providers/factory.hpp"

#include "core/providers/core.hpp"
#include "core/providers/local.hpp"

namespace px::modrim::providers {

ProviderList createProviders(const PathConfig& paths, QThreadPool* pool) {
  ProviderList providers;
  if (!paths.game.empty()) {
    providers.push_back(
        std::make_unique<CoreModProvider>(QString::fromStdString(paths.game)));
  }
  if (!paths.local.empty()) {
    const QString local = QString::fromStdString(paths.local);
    providers.push_back(std::make_unique<LocalModProvider>(local, pool));
    providers.push_back(std::make_unique<DownloadedModProvider>(local, pool));
  }
  if (!paths.workshop.empty()) {
    providers.push_back(std::make_unique<SteamWorkshopModProvider>(
        QString::fromStdString(paths.workshop), pool));
  }
  return providers;
}

}  // namespace px::modrim::providers
