#pragma once

#include <QString>
#include <optional>

#include <QCoro/QCoroTask>

#include "core/config.hpp"
#include "core/error.hpp"
#include "core/loading.hpp"
#include "core/sort/models.hpp"

class QNetworkAccessManager;

namespace px::modrim::sort {

// Keeps the community rules file in the config directory, downloading the
// Community-Rules-Database archive when the file is missing.
class CommunityRulesService {
 public:
  // `network` is not owned and must outlive the service.
  CommunityRulesService(const ConfigService& configService,
                        QNetworkAccessManager& network);

  // Returns the rules file path, downloading first when it is missing or
  // `force` is set. nullopt when the download or extraction failed, or the
  // archive has no rules file; the failure is logged, not returned.
  QCoro::Task<std::optional<QString>> ensureRules(LoadingState& loading,
                                                  bool force = false);

  Result<CommunityRules> loadRules() const;

 private:
  QCoro::Task<Result<std::optional<QString>>> downloadRules(
      LoadingState& loading);

  QString m_cacheDir;
  QString m_jsonPath;
  QNetworkAccessManager& m_network;
};

}  // namespace px::modrim::sort
