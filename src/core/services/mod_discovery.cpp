#include "core/services/mod_discovery.h"

#include <algorithm>
#include <memory>

#include <QDir>
#include <QHash>

#include "core/mods_config.h"

namespace px::core::services {

namespace {

QString strip_steam(QString value) {
    if (value.endsWith(QLatin1String("_steam"), Qt::CaseInsensitive)) {
        value.chop(6);
    }
    return value.toLower();
}

}  // namespace

QStringList resolve_active_uuids(const QHash<QString, ListedModPtr>& all_mods,
                                 const QString& config_folder) {
    if (config_folder.isEmpty()) {
        return {};
    }
    const QString path = QDir(config_folder).filePath(QStringLiteral("ModsConfig.xml"));
    const auto data = parse_mods_config(path);
    if (!data) {
        return {};
    }

    // Sort UUIDs so the first-match tiebreaker is deterministic across hash iterations.
    QStringList uuids = all_mods.keys();
    std::ranges::sort(uuids);

    QHash<QString, QString> pid_to_uuid;
    for (const QString& uuid : uuids) {
        const auto mod = std::dynamic_pointer_cast<AboutXmlMod>(all_mods.value(uuid));
        if (!mod) {
            continue;
        }
        const QString pid = strip_steam(mod->package_id.str());
        if (!pid_to_uuid.contains(pid)) {
            pid_to_uuid.insert(pid, uuid);
        }
    }

    QStringList active;
    for (const CaseInsensitiveStr& pid : data->activeMods) {
        const QString normalized = strip_steam(pid.str());
        if (const auto found = pid_to_uuid.constFind(normalized); found != pid_to_uuid.constEnd()) {
            active.append(*found);
        }
    }
    return active;
}

}  // namespace px::core::services
