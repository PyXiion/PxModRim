#pragma once

#include <optional>

#include <QCoro/QCoroTask>
#include <QString>

#include "core/config.h"
#include "core/error.h"
#include "core/loading.h"
#include "core/sort/models.h"

class QNetworkAccessManager;

namespace px::modrim::sort {

// Keeps the community rules file in the config directory, downloading the Community-Rules-Database
// archive when the file is missing.
class CommunityRulesService {
public:
    // `network` is not owned and must outlive the service.
    CommunityRulesService(const ConfigService& config_service, QNetworkAccessManager& network);

    // Returns the rules file path, downloading first when it is missing or `force` is set.
    // nullopt when the download or extraction failed, or the archive has no rules file; the
    // failure is logged, not returned.
    QCoro::Task<std::optional<QString>> ensure_rules(LoadingState& loading, bool force = false);

    Result<CommunityRules> load_rules() const;

private:
    QCoro::Task<Result<std::optional<QString>>> download_rules(LoadingState& loading);

    QString cache_dir_;
    QString json_path_;
    QNetworkAccessManager& network_;
};

}  // namespace px::modrim::sort
