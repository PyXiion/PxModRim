#include "core/services/mod_discovery.hpp"

#include <QDir>
#include <QHash>
#include <algorithm>
#include <memory>

#include "core/mods_config.hpp"

namespace px::modrim::services {

namespace {

QString stripSteam(QString value) {
  if (value.endsWith(QLatin1String("_steam"), Qt::CaseInsensitive)) {
    value.chop(6);
  }
  return value.toLower();
}

}  // namespace

QStringList resolveActiveUuids(const QHash<QString, ListedModPtr>& allMods,
                               const QString& configFolder) {
  if (configFolder.isEmpty()) {
    return {};
  }
  const QString path =
      QDir(configFolder).filePath(QStringLiteral("ModsConfig.xml"));
  const auto data = parseModsConfig(path);
  if (!data) {
    return {};
  }

  // Sort UUIDs so the first-match tiebreaker is deterministic across hash
  // iterations.
  QStringList uuids = allMods.keys();
  std::ranges::sort(uuids);

  QHash<QString, QString> pidToUuid;
  for (const QString& uuid : uuids) {
    const auto mod =
        std::dynamic_pointer_cast<AboutXmlMod>(allMods.value(uuid));
    if (!mod) {
      continue;
    }
    const QString pid = stripSteam(mod->packageId.str());
    if (!pidToUuid.contains(pid)) {
      pidToUuid.insert(pid, uuid);
    }
  }

  QStringList active;
  for (const CaseInsensitiveStr& pid : data->activeMods) {
    const QString normalized = stripSteam(pid.str());
    if (const auto found = pidToUuid.constFind(normalized);
        found != pidToUuid.constEnd()) {
      active.append(*found);
    }
  }
  return active;
}

}  // namespace px::modrim::services
