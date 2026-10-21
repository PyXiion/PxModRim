#pragma once

#include "core/providers/base.hpp"

namespace px::modrim::providers {

// Mods in the direct subdirectories of one folder, restricted to those that do
// (or do not) carry a published file id.
class FolderModProvider final : public BaseModProvider {
 public:
  FolderModProvider(QString providerId, QString color, QString path,
                    bool hasPublishedFileId, QThreadPool* pool = nullptr);

 protected:
  QList<ModDirectory> scanDirectories() const override;

 private:
  bool m_hasPublishedFileId;
};

}  // namespace px::modrim::providers
