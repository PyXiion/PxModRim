#include "core/sort/community.hpp"

#include <QFileInfo>

#include <glaze/glaze.hpp>

#include "core/config.hpp"
#include "core/file_io.hpp"

namespace px::modrim::sort {

namespace {

CaseInsensitiveSet toPackageSet(
    const std::map<std::string, SubExternalRule>& rules) {
  CaseInsensitiveSet result;
  for (const auto& [pid, _] : rules) {
    result.add(QString::fromStdString(pid));
  }
  return result;
}

}  // namespace

CommunityRules toCommunityRules(const ExternalRulesSchema& schema) {
  CommunityRules result;
  for (const auto& [pid_str, rule] : schema.rules) {
    CommunityRule community;
    community.packageId = PackageId(QString::fromStdString(pid_str));
    community.loadAfter = toPackageSet(rule.loadAfter);
    community.loadBefore = toPackageSet(rule.loadBefore);
    community.loadFirst = rule.loadTop.value;
    community.loadLast = rule.loadBottom.value;
    result.insert(community.packageId, std::move(community));
  }
  return result;
}

Result<CommunityRules> loadCommunityRules(const QString& jsonPath) {
  if (!QFileInfo::exists(jsonPath)) {
    return CommunityRules{};
  }

  auto bytes = readFileBytes(jsonPath);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }

  ExternalRulesSchema schema;
  if (auto error = glz::read<kJsonReadOpts>(schema, *bytes)) {
    return std::unexpected(Error{
        QStringLiteral("Failed to parse community rules %1: %2")
            .arg(jsonPath,
                 QString::fromStdString(glz::format_error(error, *bytes)))});
  }
  return toCommunityRules(schema);
}

}  // namespace px::modrim::sort
