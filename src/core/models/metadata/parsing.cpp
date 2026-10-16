#include "core/models/metadata/parsing.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <array>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/constants.hpp"
#include "core/utils.hpp"
#include "core/xml.hpp"

namespace px::modrim {
namespace {

std::optional<QStringList> listText(pugi::xml_node node) {
  if (!node.child("li")) {
    return std::nullopt;
  }
  QStringList result;
  for (const auto item : node.children("li")) {
    auto text = rawText(item).trimmed();
    if (!text.isEmpty()) {
      result.append(std::move(text));
    }
  }
  return result;
}

QString scalarText(pugi::xml_node node) {
  if (node.child("li")) {
    return {};
  }
  return rawText(node).trimmed();
}

pugi::xml_node versionSection(pugi::xml_node parent,
                              const QString& shortVersion) {
  if (shortVersion.isEmpty()) {
    return {};
  }
  const auto prefixed = (QStringLiteral("v") + shortVersion).toUtf8();
  if (auto node = parent.child(prefixed.constData())) {
    return node;
  }
  const auto bare = shortVersion.toUtf8();
  return parent.child(bare.constData());
}

void readDependencies(pugi::xml_node container, BaseRules& rules) {
  for (const auto item : container.children("li")) {
    if (QString::fromUtf8(item.attribute("isNull").value()) ==
        QStringLiteral("True")) {
      continue;
    }
    bool hasElement = false;
    for (const auto child : item.children()) {
      if (child.type() == pugi::node_element) {
        hasElement = true;
        break;
      }
    }
    if (!item.first_attribute() && !hasElement) {
      continue;
    }
    DependencyMod dependency;
    const auto id = rawText(item.child("packageId")).trimmed();
    if (!id.isEmpty()) {
      dependency.packageId = CaseInsensitiveStr(id);
    }
    dependency.name = rawText(item.child("displayName")).trimmed();
    dependency.workshopUrl = rawText(item.child("workshopUrl")).trimmed();
    for (const auto alternative :
         item.child("alternativePackageIds").children("li")) {
      const auto text = rawText(alternative).trimmed();
      if (!text.isEmpty()) {
        dependency.alternativePackageIds.add(text);
      }
    }
    if (rules.hasDependency(dependency.packageId)) {
      spdlog::warn("Duplicate dependency {}; keeping its first declaration",
                   dependency.packageId.str().toStdString());
    } else {
      rules.dependencies.append(std::move(dependency));
    }
  }
}

void applyOfficialMetadata(AboutXmlMod& mod, const QString& declaredName) {
  const auto* official = findRimWorldDlcByPackageId(mod.packageId.str());
  if (!declaredName.isEmpty() &&
      declaredName != QStringLiteral("Unknown Mod Name") &&
      declaredName != mod.packageId.str()) {
    mod.name = declaredName;
  } else if (official) {
    if (official->appId == kRimworldSteamAppId) {
      mod.name = QStringLiteral("Core");
    } else {
      mod.name =
          QString(official->name).mid(QStringLiteral("RimWorld - ").size());
    }
  } else {
    mod.name = mod.packageId.str();
  }

  if (official && official->appId != kRimworldSteamAppId) {
    const CaseInsensitiveStr coreId(QStringLiteral("ludeon.rimworld"));
    if (!mod.aboutRules.hasDependency(coreId)) {
      const auto* core = findRimWorldDlcByPackageId(coreId.str());
      DependencyMod dependency;
      dependency.packageId = coreId;
      dependency.name = QString(core->name);
      dependency.workshopUrl = QString(core->steamUrl);
      mod.aboutRules.dependencies.append(std::move(dependency));
    }
  }
}

}  // namespace

std::shared_ptr<AboutXmlMod> createAboutModFromElement(
    pugi::xml_node root, const QString& targetVersion, bool preferVersioned) {
  auto mod = std::make_shared<AboutXmlMod>();
  const auto versionParts = targetVersion.split(QLatin1Char('.'));
  const QString shortVersion =
      versionParts.size() >= 2
          ? versionParts[0] + QLatin1Char('.') + versionParts[1]
          : QString{};
  QString declaredName;
  QString description;
  QString versionDescription;
  bool sawPackageId = false;
  pugi::xml_node dependencies;
  pugi::xml_node versionDependencies;
  constexpr std::array<const char*, 5> kRuleTags{
      "loadBefore", "loadAfter", "incompatibleWith", "forceLoadBefore",
      "forceLoadAfter"};
  constexpr std::array<const char*, 3> kVersionRuleTags{
      "loadBeforeByVersion", "loadAfterByVersion", "incompatibleWithByVersion"};
  std::array<QStringList, 5> ruleLists;
  std::array<pugi::xml_node, 3> versionRules;

  for (const auto child : root.children()) {
    if (child.type() != pugi::node_element) {
      continue;
    }
    const QLatin1StringView tag(child.name());
    if (tag == QLatin1StringView("packageId")) {
      sawPackageId = true;
      auto id = scalarText(child);
      if (id.isEmpty()) {
        mod->valid = false;
        id = QString(kDefaultMissingPackageid);
        spdlog::warn("About.xml contains an unusable packageId");
      }
      mod->packageId = CaseInsensitiveStr(id);
    } else if (tag == QLatin1StringView("steamAppId")) {
      const auto text = scalarText(child);
      if (isAsciiDigits(text)) {
        bool ok = false;
        const auto appId = text.toLongLong(&ok);
        if (ok) {
          mod->steamAppId = appId;
        }
      }
    } else if (tag == QLatin1StringView("name")) {
      const auto text = scalarText(child);
      if (!text.isEmpty()) declaredName = text;
    } else if (tag == QLatin1StringView("description")) {
      const auto text = scalarText(child);
      if (!text.isEmpty()) description = text;
    } else if (tag == QLatin1StringView("modVersion")) {
      const auto text = scalarText(child);
      if (!text.isEmpty()) mod->modVersion = text;
    } else if (tag == QLatin1StringView("url")) {
      const auto text = scalarText(child);
      if (!text.isEmpty()) mod->url = text;
    } else if (tag == QLatin1StringView("modIconPath")) {
      const auto text = scalarText(child);
      if (!text.isEmpty()) mod->modIconPath = text;
    } else if (tag == QLatin1StringView("author")) {
      const auto text = scalarText(child);
      if (!text.isEmpty()) mod->authors.append(text);
    } else if (tag == QLatin1StringView("authors")) {
      if (auto list = listText(child)) mod->authors.append(*list);
    } else if (tag == QLatin1StringView("supportedVersions")) {
      if (auto list = listText(child)) {
        mod->supportedVersions = QSet<QString>(list->begin(), list->end());
      }
    } else if (tag == QLatin1StringView("modDependencies")) {
      dependencies = child;
    } else if (tag == QLatin1StringView("modDependenciesByVersion")) {
      if (preferVersioned) {
        if (auto section = versionSection(child, shortVersion)) {
          versionDependencies = section;
        }
      }
    } else if (tag == QLatin1StringView("descriptionsByVersion")) {
      // Descriptions select their version independently of rule preferences.
      const auto text = rawText(versionSection(child, shortVersion)).trimmed();
      if (!text.isEmpty()) versionDescription = text;
    } else {
      for (size_t index = 0; index < kRuleTags.size(); ++index) {
        if (tag == QLatin1StringView(kRuleTags[index])) {
          if (auto list = listText(child)) ruleLists[index] = std::move(*list);
        }
      }
      if (preferVersioned) {
        for (size_t index = 0; index < kVersionRuleTags.size(); ++index) {
          if (tag == QLatin1StringView(kVersionRuleTags[index])) {
            if (auto section = versionSection(child, shortVersion)) {
              versionRules[index] = section;
            }
          }
        }
      }
    }
  }

  if (!sawPackageId) {
    mod->valid = false;
    mod->packageId = CaseInsensitiveStr(QString(kDefaultMissingPackageid));
    spdlog::warn("About.xml has no packageId");
  }
  if (!versionDescription.isEmpty()) {
    mod->description = std::move(versionDescription);
  } else {
    mod->description = std::move(description);
  }
  for (size_t index = 0; index < versionRules.size(); ++index) {
    if (auto list = listText(versionRules[index])) {
      ruleLists[index] = std::move(*list);
    }
  }
  ruleLists[0].append(ruleLists[3]);
  ruleLists[1].append(ruleLists[4]);
  mod->aboutRules.loadBefore = CaseInsensitiveSet(ruleLists[0]);
  mod->aboutRules.loadAfter = CaseInsensitiveSet(ruleLists[1]);
  mod->aboutRules.incompatibleWith = CaseInsensitiveSet(ruleLists[2]);
  readDependencies(versionDependencies ? versionDependencies : dependencies,
                   mod->aboutRules);
  applyOfficialMetadata(*mod, declaredName);
  return mod;
}

Result<ListedModPtr> createListedModFromPath(
    const QString& path, const QString& targetVersion, bool preferVersioned,
    std::optional<QString> aboutXmlPath) {
  if (!QFileInfo(path).isDir()) {
    return std::unexpected(
        Error{QStringLiteral("Path must be a directory: %1").arg(path)});
  }
  if (!aboutXmlPath) {
    aboutXmlPath = findAboutXml(path);
  }
  ListedModPtr mod;
  if (aboutXmlPath) {
    pugi::xml_document document;
    if (auto loaded = loadXmlFile(document, *aboutXmlPath); loaded) {
      auto about = createAboutModFromElement(document.document_element(),
                                             targetVersion, preferVersioned);
      const QFileInfo metadata(*aboutXmlPath);
      if (metadata.exists()) {
        about->mtime =
            static_cast<double>(metadata.lastModified().toMSecsSinceEpoch()) /
            1000.0;
      }
      mod = std::move(about);
    } else {
      spdlog::error("Unable to read mod metadata {}: {}",
                    aboutXmlPath->toStdString(),
                    loaded.error().message.toStdString());
      mod = std::make_shared<AboutXmlMod>();
      mod->valid = false;
    }
  } else {
    mod = std::make_shared<ListedMod>();
    mod->valid = false;
    spdlog::warn("No About.xml found for {}", path.toStdString());
  }
  if (auto assigned = mod->setModPath(path); !assigned) {
    return std::unexpected(assigned.error());
  }
  return mod;
}

}  // namespace px::modrim
