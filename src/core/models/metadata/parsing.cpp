#include "core/models/metadata/parsing.hpp"

#include <variant>

#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>

#include "core/constants.hpp"
#include "core/utils.hpp"
#include "core/xml.hpp"

namespace px::modrim {

namespace {

// Either the stripped text of an element or the stripped texts of its <li> children.
using ElementValue = std::variant<QString, QStringList>;

std::string utf8(const QString& text) { return text.toStdString(); }

void setInvalid(ListedMod& mod, const QString& message) {
    mod.valid = false;
    spdlog::warn("{}", utf8(message));
}

CaseInsensitiveStr missingPackageId() {
    return CaseInsensitiveStr(QString(kDefaultMissingPackageid));
}

// Text or <li> list of an element; nullopt when the element is absent or empty.
std::optional<ElementValue> elementValue(pugi::xml_node element) {
    QStringList items;
    bool hasItems = false;
    for (pugi::xml_node li : element.children("li")) {
        hasItems = true;
        const QString text = rawText(li).trimmed();
        if (!text.isEmpty()) {
            items.append(text);
        }
    }
    if (hasItems) {
        return ElementValue{items};
    }
    const QString text = rawText(element).trimmed();
    if (!text.isEmpty()) {
        return ElementValue{text};
    }
    return std::nullopt;
}

std::optional<QString> asText(const std::optional<ElementValue>& value) {
    if (value && std::holds_alternative<QString>(*value)) {
        return std::get<QString>(*value);
    }
    return std::nullopt;
}

std::optional<QStringList> asList(const std::optional<ElementValue>& value) {
    if (value && std::holds_alternative<QStringList>(*value)) {
        return std::get<QStringList>(*value);
    }
    return std::nullopt;
}

// The `<v{major}.{minor}>` child matching the target game version, or a null node when none
// matches or the version has no major.minor part.
pugi::xml_node matchVersionedChild(pugi::xml_node parent, const QString& targetVersion) {
    const QStringList parts = targetVersion.split(QLatin1Char('.'));
    if (parts.size() < 2) {
        return {};
    }
    const QString majorMinor = parts[0] + QLatin1Char('.') + parts[1];
    const std::string prefixed = utf8(QLatin1Char('v') + majorMinor);
    return parent.child(prefixed.c_str());
}

QString officialModName(const QString& packageId, const QString& name) {
    if (!name.isEmpty() && name != QStringLiteral("Unknown Mod Name") && name != packageId) {
        return name;
    }
    if (packageId == QLatin1StringView("ludeon.rimworld")) {
        return QStringLiteral("Core");
    }
    if (const RimWorldDlc* dlc = findRimWorldDlcByPackageId(packageId)) {
        const QString dlcName(dlc->name);
        const QString prefix = QStringLiteral("RimWorld - ");
        return dlcName.startsWith(prefix) ? dlcName.mid(prefix.size()) : dlcName;
    }
    return packageId;
}

DependencyMod dependencyFromLi(pugi::xml_node li) {
    DependencyMod dep;
    if (const auto packageId = childText(li, "packageId")) {
        dep.packageId = CaseInsensitiveStr(*packageId);
    }
    if (const auto name = childText(li, "displayName")) {
        dep.name = *name;
    }
    if (const auto url = childText(li, "workshopUrl")) {
        dep.workshopUrl = *url;
    }
    if (pugi::xml_node alternatives = li.child("alternativePackageIds")) {
        for (pugi::xml_node alternative : alternatives.children("li")) {
            const QString text = rawText(alternative).trimmed();
            if (!text.isEmpty()) {
                dep.alternativePackageIds.add(text);
            }
        }
    }
    return dep;
}

// lxml's `len(li)` counts comments as well as elements, but not text.
bool hasChildNodes(pugi::xml_node node) {
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_pcdata && child.type() != pugi::node_cdata) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<AboutXmlMod> aboutModWithoutXml() {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->valid = false;
    return mod;
}

template <class T>
Result<std::shared_ptr<T>> withModPath(std::shared_ptr<T> mod, const QString& path) {
    if (auto set = mod->setModPath(path); !set) {
        return std::unexpected(set.error());
    }
    return mod;
}

Result<ListedModPtr> invalidListedMod(const QString& path) {
    auto mod = std::make_shared<ListedMod>();
    mod->valid = false;
    return withModPath<ListedMod>(std::move(mod), path);
}

// Mirrors the legacy `_create_about_mod_from_xml`: unreadable or malformed files still yield an
// invalid AboutXmlMod that carries the mod path, so the caller can cache and report it.
Result<std::shared_ptr<AboutXmlMod>> createAboutModFromXml(const QString& basePath,
                                                           const QString& aboutXmlPath,
                                                           const QString& targetVersion,
                                                           bool preferVersioned) {
    pugi::xml_document doc;
    if (auto loaded = loadXmlFile(doc, aboutXmlPath); !loaded) {
        spdlog::error("Unable to parse {}: {}", utf8(aboutXmlPath), utf8(loaded.error().message));
        return withModPath<AboutXmlMod>(aboutModWithoutXml(), basePath);
    }

    auto mod = createAboutModFromElement(doc.document_element(), targetVersion, preferVersioned);
    auto withPath = withModPath<AboutXmlMod>(std::move(mod), basePath);
    if (!withPath) {
        return withPath;
    }
    const QFileInfo info(aboutXmlPath);
    (*withPath)->mtime = info.exists() ? info.lastModified().toMSecsSinceEpoch() / 1000.0 : 0.0;
    return withPath;
}

}  // namespace

std::shared_ptr<AboutXmlMod> createAboutModFromElement(pugi::xml_node root,
                                                       const QString& targetVersion,
                                                       bool preferVersioned) {
    auto mod = std::make_shared<AboutXmlMod>();
    bool hasPackageId = false;

    pugi::xml_node depsEl;
    bool depsByVersion = false;
    QStringList loadBeforeLi;
    QStringList loadAfterLi;
    QStringList incompatLi;
    QStringList forceBeforeLi;
    QStringList forceAfterLi;
    pugi::xml_node loadBeforeByVersionEl;
    pugi::xml_node loadAfterByVersionEl;
    pugi::xml_node incompatByVersionEl;

    for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string_view tag = child.name();

        if (tag == "packageId") {
            hasPackageId = true;
            const auto text = asText(elementValue(child));
            if (text && !text->isEmpty()) {
                mod->packageId = CaseInsensitiveStr(*text);
            } else {
                setInvalid(*mod, QStringLiteral("packageId missing or invalid. Assigned sentinel "
                                                "'%1'.")
                                    .arg(QString(kDefaultMissingPackageid)));
                mod->packageId = missingPackageId();
            }
        } else if (tag == "steamAppId") {
            const auto text = asText(elementValue(child));
            if (text && isAsciiDigits(*text)) {
                bool ok = false;
                const qint64 appId = text->toLongLong(&ok);
                if (ok) {
                    mod->steamAppId = appId;
                }
            }
        } else if (tag == "name") {
            if (const auto text = asText(elementValue(child))) {
                mod->name = *text;
            }
        } else if (tag == "description") {
            if (const auto text = asText(elementValue(child))) {
                mod->description = *text;
            }
        } else if (tag == "author") {
            if (const auto text = asText(elementValue(child))) {
                mod->authors.append(*text);
            }
        } else if (tag == "authors") {
            if (const auto list = asList(elementValue(child))) {
                mod->authors.append(*list);
            }
        } else if (tag == "supportedVersions") {
            if (const auto list = asList(elementValue(child))) {
                mod->supportedVersions = QSet<QString>(list->cbegin(), list->cend());
            }
        } else if (tag == "modVersion") {
            if (const auto text = asText(elementValue(child))) {
                mod->modVersion = *text;
            }
        } else if (tag == "modIconPath") {
            if (const auto text = asText(elementValue(child))) {
                mod->modIconPath = *text;
            }
        } else if (tag == "url") {
            if (const auto text = asText(elementValue(child))) {
                mod->url = *text;
            }
        } else if (tag == "modDependencies") {
            if (!depsByVersion) {
                depsEl = child;
            }
        } else if (tag == "modDependenciesByVersion") {
            if (preferVersioned) {
                if (const pugi::xml_node matched = matchVersionedChild(child, targetVersion)) {
                    depsEl = matched;
                    depsByVersion = true;
                }
            }
        } else if (tag == "loadBefore") {
            if (const auto list = asList(elementValue(child))) {
                loadBeforeLi = *list;
            }
        } else if (tag == "loadBeforeByVersion") {
            if (preferVersioned) {
                if (const pugi::xml_node matched = matchVersionedChild(child, targetVersion)) {
                    loadBeforeByVersionEl = matched;
                }
            }
        } else if (tag == "loadAfter") {
            if (const auto list = asList(elementValue(child))) {
                loadAfterLi = *list;
            }
        } else if (tag == "loadAfterByVersion") {
            if (preferVersioned) {
                if (const pugi::xml_node matched = matchVersionedChild(child, targetVersion)) {
                    loadAfterByVersionEl = matched;
                }
            }
        } else if (tag == "forceLoadBefore") {
            if (const auto list = asList(elementValue(child))) {
                forceBeforeLi = *list;
            }
        } else if (tag == "forceLoadAfter") {
            if (const auto list = asList(elementValue(child))) {
                forceAfterLi = *list;
            }
        } else if (tag == "incompatibleWith") {
            if (const auto list = asList(elementValue(child))) {
                incompatLi = *list;
            }
        } else if (tag == "incompatibleWithByVersion") {
            if (preferVersioned) {
                if (const pugi::xml_node matched = matchVersionedChild(child, targetVersion)) {
                    incompatByVersionEl = matched;
                }
            }
        } else if (tag == "descriptionsByVersion") {
            if (const pugi::xml_node matched = matchVersionedChild(child, targetVersion)) {
                const QString text = rawText(matched).trimmed();
                if (!text.isEmpty()) {
                    mod->description = text;
                }
            }
        }
    }

    if (!hasPackageId) {
        setInvalid(*mod, QStringLiteral("packageId missing or invalid. Assigned sentinel '%1'.")
                             .arg(QString(kDefaultMissingPackageid)));
        mod->packageId = missingPackageId();
    }

    // Versioned overrides replace the unversioned lists they shadow.
    if (loadBeforeByVersionEl) {
        if (const auto list = asList(elementValue(loadBeforeByVersionEl))) {
            loadBeforeLi = *list;
        }
    }
    if (loadAfterByVersionEl) {
        if (const auto list = asList(elementValue(loadAfterByVersionEl))) {
            loadAfterLi = *list;
        }
    }
    if (incompatByVersionEl) {
        if (const auto list = asList(elementValue(incompatByVersionEl))) {
            incompatLi = *list;
        }
    }

    loadBeforeLi.append(forceBeforeLi);
    loadAfterLi.append(forceAfterLi);

    BaseRules rules;
    rules.loadBefore = CaseInsensitiveSet(loadBeforeLi);
    rules.loadAfter = CaseInsensitiveSet(loadAfterLi);
    rules.incompatibleWith = CaseInsensitiveSet(incompatLi);

    if (depsEl) {
        for (pugi::xml_node li = depsEl.first_child(); li; li = li.next_sibling()) {
            if (li.type() != pugi::node_element || std::string_view(li.name()) != "li") {
                continue;
            }
            if (std::string_view(li.attribute("isNull").value()) == "True") {
                continue;
            }
            if (!li.first_attribute() && !hasChildNodes(li)) {
                continue;
            }
            DependencyMod dep = dependencyFromLi(li);
            if (rules.findDependency(dep.packageId)) {
                spdlog::warn("Duplicate dependency found: {}. Skipping.",
                             utf8(dep.packageId.str()));
            } else {
                rules.dependencies.append(std::move(dep));
            }
        }
    }

    const QString packageId = mod->packageId.str();
    mod->name = officialModName(packageId, mod->name);
    const RimWorldDlc* dlc = findRimWorldDlcByPackageId(packageId);
    if (dlc && mod->description.isEmpty()) {
        mod->description = QString(dlc->description);
    }
    // DLC cannot load without the base game, so it always depends on RimWorld.
    if (dlc && QLatin1StringView(dlc->appId) != kRimworldSteamAppId) {
        const CaseInsensitiveStr rimworldPid(QStringLiteral("ludeon.rimworld"));
        if (!rules.hasDependency(rimworldPid)) {
            DependencyMod rimworld;
            rimworld.name = QStringLiteral("RimWorld");
            rimworld.packageId = rimworldPid;
            rimworld.workshopUrl = QStringLiteral("https://store.steampowered.com/app/294100/RimWorld");
            rules.dependencies.append(std::move(rimworld));
        }
    }

    mod->aboutRules = std::move(rules);
    return mod;
}

Result<ListedModPtr> createListedModFromPath(const QString& path, const QString& targetVersion,
                                             bool preferVersioned,
                                             std::optional<QString> aboutXmlPath) {
    if (!QFileInfo(path).isDir()) {
        return std::unexpected(Error{QStringLiteral("Path must be a directory: %1").arg(path)});
    }
    if (!aboutXmlPath) {
        aboutXmlPath = findAboutXml(path);
    }
    if (aboutXmlPath) {
        auto mod = createAboutModFromXml(path, *aboutXmlPath, targetVersion, preferVersioned);
        if (!mod) {
            return std::unexpected(mod.error());
        }
        return ListedModPtr(std::move(*mod));
    }

    const QStringList rscFiles = QDir(path).entryList(
        {QStringLiteral("*.rsc")},
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::CaseSensitive);
    if (rscFiles.size() > 1) {
        spdlog::warn("Multiple .rsc files found in {}. Aborting.", utf8(path));
        return invalidListedMod(path);
    }
    if (rscFiles.size() == 1) {
        // Scenario .rsc files are not parsed yet.
        spdlog::warn("Scenario .rsc files not yet supported: {}", utf8(path));
        return invalidListedMod(path);
    }
    spdlog::warn("No About.xml found in directory: {}", utf8(path));
    return invalidListedMod(path);
}

}  // namespace px::modrim
