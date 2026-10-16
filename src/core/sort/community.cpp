#include "core/sort/community.h"

#include <QFileInfo>

#include <glaze/glaze.hpp>

#include "core/config.h"
#include "core/file_io.h"

namespace px::modrim::sort {

namespace {

CaseInsensitiveSet to_package_set(const std::map<std::string, SubExternalRule>& rules) {
    CaseInsensitiveSet result;
    for (const auto& [pid, _] : rules) {
        result.add(QString::fromStdString(pid));
    }
    return result;
}

}  // namespace

CommunityRules to_community_rules(const ExternalRulesSchema& schema) {
    CommunityRules result;
    for (const auto& [pid_str, rule] : schema.rules) {
        CommunityRule community;
        community.package_id = PackageId(QString::fromStdString(pid_str));
        community.load_after = to_package_set(rule.loadAfter);
        community.load_before = to_package_set(rule.loadBefore);
        community.load_first = rule.loadTop.value;
        community.load_last = rule.loadBottom.value;
        result.insert(community.package_id, std::move(community));
    }
    return result;
}

Result<CommunityRules> load_community_rules(const QString& json_path) {
    if (!QFileInfo::exists(json_path)) {
        return CommunityRules{};
    }

    auto bytes = read_file_bytes(json_path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    ExternalRulesSchema schema;
    if (auto error = glz::read<kJsonReadOpts>(schema, *bytes)) {
        return std::unexpected(Error{QStringLiteral("Failed to parse community rules %1: %2")
                                         .arg(json_path, QString::fromStdString(
                                                             glz::format_error(error, *bytes)))});
    }
    return to_community_rules(schema);
}

}  // namespace px::modrim::sort
