#include "core/models/metadata/structures.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include "core/constants.h"
#include "core/utils.h"

namespace px::core {

namespace {

// Python's Path.stem / Path.suffix rule: a leading dot does not start an extension.
qsizetype lastExtensionDot(const QString& name) {
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return (dot > 0 && dot < name.size() - 1) ? dot : -1;
}

QString pathStem(const QString& name) {
    const qsizetype dot = lastExtensionDot(name);
    return dot < 0 ? name : name.left(dot);
}

QString pathSuffix(const QString& name) {
    const qsizetype dot = lastExtensionDot(name);
    return dot < 0 ? QString() : name.mid(dot);
}

bool hasEntries(const QString& dirPath) {
    return !QDir(dirPath)
                .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
                .isEmpty();
}

}  // namespace

CaseInsensitiveSet::CaseInsensitiveSet(const QStringList& values) {
    for (const QString& value : values) {
        data_.insert(CaseInsensitiveStr(value));
    }
}

CaseInsensitiveSet::CaseInsensitiveSet(const QList<CaseInsensitiveStr>& values) {
    for (const CaseInsensitiveStr& value : values) {
        data_.insert(value);
    }
}

bool CaseInsensitiveSet::contains(QStringView value) const {
    return data_.contains(CaseInsensitiveStr(value));
}

bool CaseInsensitiveSet::contains(const CaseInsensitiveStr& value) const {
    return data_.contains(value);
}

void CaseInsensitiveSet::add(QStringView value) { data_.insert(CaseInsensitiveStr(value)); }

void CaseInsensitiveSet::add(const CaseInsensitiveStr& value) { data_.insert(value); }

void CaseInsensitiveSet::discard(QStringView value) { data_.remove(CaseInsensitiveStr(value)); }

void CaseInsensitiveSet::discard(const CaseInsensitiveStr& value) { data_.remove(value); }

CaseInsensitiveSet CaseInsensitiveSet::operator|(const CaseInsensitiveSet& other) const {
    CaseInsensitiveSet result = *this;
    result.data_.unite(other.data_);
    return result;
}

bool ModsConfig::check_active_duplicates() const {
    const QSet<CaseInsensitiveStr> unique(activeMods.cbegin(), activeMods.cend());
    return activeMods.size() != unique.size();
}

bool ModsConfig::check_expansions_duplicates() const {
    const QSet<CaseInsensitiveStr> unique(knownExpansions.cbegin(), knownExpansions.cend());
    return knownExpansions.size() != unique.size();
}

const DependencyMod* BaseRules::find_dependency(const CaseInsensitiveStr& package_id) const {
    for (const DependencyMod& dependency : dependencies) {
        if (dependency.package_id == package_id) {
            return &dependency;
        }
    }
    return nullptr;
}

void BaseRules::set_dependency(DependencyMod dependency) {
    for (DependencyMod& existing : dependencies) {
        if (existing.package_id == dependency.package_id) {
            existing = std::move(dependency);
            return;
        }
    }
    dependencies.append(std::move(dependency));
}

QString ListedMod::newUuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString ListedMod::uuid() const {
    if (mod_path_) {
        return *mod_path_;
    }
    return uuid_;
}

Result<void> ListedMod::set_mod_path(const QString& path) {
    if (mod_path_) {
        return std::unexpected(Error{QStringLiteral("Mod path already set. Cannot override.")});
    }
    mod_path_ = path;
    published_file_id_cache_.reset();
    return {};
}

std::optional<QString> ListedMod::mod_folder() const {
    if (!mod_path_) {
        return std::nullopt;
    }
    return pathStem(QFileInfo(QDir::cleanPath(*mod_path_)).fileName());
}

qint64 ListedMod::internal_time_touched() const {
    if (!mod_path_ || !QFileInfo::exists(*mod_path_)) {
        return -1;
    }
    return QFileInfo(*mod_path_).lastModified().toSecsSinceEpoch();
}

std::optional<QString> ListedMod::published_file_id() const {
    if (!published_file_id_cache_) {
        published_file_id_cache_ = [this]() -> std::optional<QString> {
            if (!mod_path_) {
                return std::nullopt;
            }
            const QString pfidPath =
                QDir(*mod_path_).filePath(QStringLiteral("About/PublishedFileId.txt"));
            if (QFileInfo::exists(pfidPath)) {
                QFile file(pfidPath);
                if (!file.open(QIODevice::ReadOnly)) {
                    return std::nullopt;
                }
                // utf-8-sig: drop a leading byte-order mark before trimming.
                QString content = QString::fromUtf8(file.readAll());
                if (content.startsWith(QChar(0xFEFF))) {
                    content.remove(0, 1);
                }
                content = content.trimmed();
                if (isAsciiDigits(content)) {
                    return content;
                }
            }
            const std::optional<QString> folder = mod_folder();
            if (folder && isAsciiDigits(*folder)) {
                return folder;
            }
            return std::nullopt;
        }();
    }
    return *published_file_id_cache_;
}

bool ListedMod::c_sharp_mod() const {
    if (!c_sharp_mod_cache_) {
        c_sharp_mod_cache_ = [this] {
            if (!mod_path_) {
                return false;
            }
            const QString assemblies = QDir(*mod_path_).filePath(QStringLiteral("Assemblies"));
            return QFileInfo(assemblies).isDir() && hasEntries(assemblies);
        }();
    }
    return *c_sharp_mod_cache_;
}

bool ListedMod::xml_patch_mod() const {
    if (!xml_patch_mod_cache_) {
        xml_patch_mod_cache_ = [this] {
            if (!mod_path_) {
                return false;
            }
            const QString patches = QDir(*mod_path_).filePath(QStringLiteral("Patches"));
            if (!QFileInfo(patches).isDir()) {
                return false;
            }
            const QFileInfoList entries = QDir(patches).entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
            for (const QFileInfo& entry : entries) {
                if (pathSuffix(entry.fileName()) == QStringLiteral(".xml")) {
                    return true;
                }
            }
            return false;
        }();
    }
    return *xml_patch_mod_cache_;
}

std::optional<QString> ListedMod::preview_img_path() const {
    if (!mod_path_) {
        return std::nullopt;
    }
    const QString candidate = QDir(*mod_path_).filePath(QStringLiteral("About/Preview.png"));
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
    return std::nullopt;
}

std::optional<QString> AboutXmlMod::get_dlc_name() const {
    if (const RimWorldDlc* dlc = findRimWorldDlcByPackageId(package_id.str())) {
        return QString(dlc->name);
    }
    return std::nullopt;
}

}  // namespace px::core
