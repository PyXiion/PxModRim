#include "core/providers/factory.hpp"

#include "core/providers/core.hpp"
#include "core/providers/folder.hpp"

namespace px::modrim::providers {

ProviderList createProviders(const PathConfig& paths, QThreadPool* pool) {
  ProviderList providers;
  if (!paths.game.empty()) {
    providers.push_back(
        std::make_unique<CoreModProvider>(QString::fromStdString(paths.game)));
  }
  if (!paths.local.empty()) {
    const QString local = QString::fromStdString(paths.local);
    providers.push_back(std::make_unique<FolderModProvider>(
        QStringLiteral("local"), QStringLiteral("#2ecc71"), local, false,
        pool));
    providers.push_back(std::make_unique<FolderModProvider>(
        QStringLiteral("downloaded"), QStringLiteral("#3498db"), local, true,
        pool));
  }
  if (!paths.workshop.empty()) {
    providers.push_back(std::make_unique<FolderModProvider>(
        QStringLiteral("steam"), QStringLiteral("#3498db"),
        QString::fromStdString(paths.workshop), true, pool));
  }
  return providers;
}

}  // namespace px::modrim::providers
