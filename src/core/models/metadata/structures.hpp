#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <glaze/glaze.hpp>

#include "core/error.hpp"

namespace px::modrim {

// Package id that compares and hashes without regard to case. Stored lowercased.
class CaseInsensitiveStr {
public:
    CaseInsensitiveStr() = default;
    explicit CaseInsensitiveStr(QStringView pid) : value_(pid.toString().toLower()) {}

    const QString& str() const noexcept { return value_; }

    friend bool operator==(const CaseInsensitiveStr&, const CaseInsensitiveStr&) = default;
    friend size_t qHash(const CaseInsensitiveStr& key, size_t seed = 0) noexcept {
        return qHash(key.value_, seed);
    }

private:
    QString value_;
};

// Set of CaseInsensitiveStr with string-friendly membership tests.
class CaseInsensitiveSet {
public:
    CaseInsensitiveSet() = default;
    explicit CaseInsensitiveSet(const QStringList& values);
    explicit CaseInsensitiveSet(const QList<CaseInsensitiveStr>& values);

    bool contains(QStringView value) const;
    bool contains(const CaseInsensitiveStr& value) const;
    void add(QStringView value);
    void add(const CaseInsensitiveStr& value);
    void discard(QStringView value);
    void discard(const CaseInsensitiveStr& value);

    qsizetype size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.isEmpty(); }

    // Set union; the result keeps the elements of both operands.
    CaseInsensitiveSet operator|(const CaseInsensitiveSet& other) const;

    auto begin() const { return data_.cbegin(); }
    auto end() const { return data_.cend(); }

    friend bool operator==(const CaseInsensitiveSet&, const CaseInsensitiveSet&) = default;

private:
    QSet<CaseInsensitiveStr> data_;
};

// Parsed ModsConfig.xml data with active mods and known expansions.
struct ModsConfig {
    QString version;
    QList<CaseInsensitiveStr> activeMods;
    QList<CaseInsensitiveStr> knownExpansions;

    bool check_active_duplicates() const;
    bool check_expansions_duplicates() const;
    void clear_active_mods() { activeMods.clear(); }
    void clear_all() {
        activeMods.clear();
        knownExpansions.clear();
    }
};

struct ReplacementInfo {
    QString name;
    QString author;
    QString packageid;
    QString pfid;
    QStringList supportedversions;
    QString source = QStringLiteral("database");
};

// Dependency declared by a mod's About.xml (or synthesized, e.g. the RimWorld base game for DLC).
struct DependencyMod {
    QString name;
    CaseInsensitiveStr package_id{QStringLiteral("invalid.mod")};
    QString workshop_url;
    CaseInsensitiveSet alternative_package_ids;
};

// Load-order rules and dependencies. Dependencies keep declaration order.
struct BaseRules {
    CaseInsensitiveSet load_after;
    CaseInsensitiveSet load_before;
    CaseInsensitiveSet incompatible_with;
    QList<DependencyMod> dependencies;

    const DependencyMod* find_dependency(const CaseInsensitiveStr& package_id) const;
    bool has_dependency(const CaseInsensitiveStr& package_id) const {
        return find_dependency(package_id) != nullptr;
    }
    // Inserts, or replaces the dependency with the same package id in place.
    void set_dependency(DependencyMod dependency);
};

// Mod discovered by any provider. Polymorphic so providers can hand out AboutXmlMod (and
// ScenarioMod) through a shared base pointer.
class ListedMod {
public:
    ListedMod() = default;
    ListedMod(const ListedMod&) = default;
    ListedMod& operator=(const ListedMod&) = default;
    virtual ~ListedMod() = default;

    QString name = QStringLiteral("Unknown Mod Name");
    bool valid = true;
    QSet<QString> supported_versions;
    QString description = QStringLiteral(
        "This mod is considered invalid by PxModRim (and the RimWorld game).\n\n"
        "This mod does NOT contain an ./About/About.xml and is likely leftover from previous "
        "usage.");
    QString provider_id;
    bool obsolete = false;
    bool db_builder_no_name = false;

    // Identity: the mod path once it is known, otherwise a random id assigned at construction.
    virtual QString uuid() const;

    const std::optional<QString>& mod_path() const noexcept { return mod_path_; }
    // Fails if a path was already set; a mod's directory is fixed after discovery.
    Result<void> set_mod_path(const QString& path);

    // Folder name without its last extension (Python's Path.stem); nullopt without a path.
    std::optional<QString> mod_folder() const;
    // Modification time of the mod folder in seconds since the epoch, or -1 if unavailable.
    qint64 internal_time_touched() const;

    // Published file id from About/PublishedFileId.txt or a numeric folder name. Cached on first
    // use; this touches the disk, so callers may warm it off the UI thread.
    std::optional<QString> published_file_id() const;
    bool c_sharp_mod() const;
    bool xml_patch_mod() const;
    std::optional<QString> preview_img_path() const;

private:
    static QString newUuid();

    QString uuid_ = newUuid();
    std::optional<QString> mod_path_;
    mutable std::optional<std::optional<QString>> published_file_id_cache_;
    mutable std::optional<bool> c_sharp_mod_cache_;
    mutable std::optional<bool> xml_patch_mod_cache_;
};

class ScenarioMod : public ListedMod {
public:
    QString summary;
};

// A mod with an About.xml: authors, version, rules, and DLC metadata.
class AboutXmlMod : public ListedMod {
public:
    CaseInsensitiveStr package_id{QStringLiteral("invalid.mod")};
    QStringList authors;
    QString mod_version;
    std::optional<QString> mod_icon_path;
    qint64 steam_app_id = -1;
    QString url;
    double mtime = 0.0;
    BaseRules about_rules;

    // Display name of the RimWorld DLC this package id belongs to, if any.
    std::optional<QString> get_dlc_name() const;
};

using ListedModPtr = std::shared_ptr<ListedMod>;

// Pre-computed dependency graphs and tier classification for sorting.
struct CompiledDependencyData {
    QHash<QString, QSet<QString>> deps_graph;
    QHash<QString, QSet<QString>> rev_deps_graph;
    QSet<QString> tier_zero_mods;
    QSet<QString> tier_one_mods;
    QSet<QString> tier_three_mods;
    QHash<QString, QSet<QString>> incompatibilities;
    QHash<QString, QSet<QString>> declared_incompatibilities;
};

// Glaze schemas for community rules and the Steam Workshop database. Fields the upstream JSON
// may write either as a single value or as a list use std::variant.

struct SubExternalRule {
    std::variant<std::vector<std::string>, std::string> name = std::string{};
    std::variant<std::vector<std::string>, std::string> comment = std::string{};
};

struct SubExternalBoolRule {
    bool value = false;
    std::variant<std::vector<std::string>, std::string> comment = std::string{};
};

struct ExternalRule {
    std::map<std::string, SubExternalRule> loadAfter;
    std::map<std::string, SubExternalRule> loadBefore;
    SubExternalBoolRule loadTop;
    SubExternalBoolRule loadBottom;
};

struct ExternalRulesSchema {
    std::int64_t timestamp = 0;
    std::map<std::string, ExternalRule> rules;
};

struct SteamDbEntryDependency {
    std::string name;
    std::string url;
};

struct SteamDbEntryBlacklist {
    bool value = false;
    std::string comment;
};

struct SteamDbEntry {
    bool unpublished = false;
    std::string url;
    std::string packageId;
    std::variant<std::vector<std::optional<std::string>>, std::string, std::nullptr_t>
        gameVersions = std::vector<std::optional<std::string>>{};
    std::string steamName;
    std::string name;
    std::variant<std::vector<std::string>, std::string, std::nullptr_t> authors = std::string{};
    std::map<std::string, std::variant<std::vector<std::string>, SteamDbEntryDependency>>
        dependencies;
    SteamDbEntryBlacklist blacklist;
    std::vector<std::map<std::string, std::string>> tags;
};

struct SteamDbSchema {
    std::int64_t version = 0;
    std::map<std::string, SteamDbEntry> database;
};

enum class WorkshopUpdateStatus {
    Success,
    NoWorkshopMods,
    Partial,
    Failed,
};

struct WorkshopUpdateResult {
    WorkshopUpdateStatus status = WorkshopUpdateStatus::Failed;
    int mods_checked = 0;
    int mods_updated = 0;
    QStringList failed_pfids;
    QStringList errors;
};

}  // namespace px::modrim

// Package ids are JSON strings on the wire.
template <>
struct glz::to<glz::JSON, px::modrim::CaseInsensitiveStr> {
    template <auto Opts>
    static void op(const px::modrim::CaseInsensitiveStr& value, auto&& ctx, auto&& b, auto&& ix) {
        glz::to<glz::JSON, std::string>::template op<Opts>(value.str().toStdString(), ctx, b, ix);
    }
};

template <>
struct glz::from<glz::JSON, px::modrim::CaseInsensitiveStr> {
    template <auto Opts>
    static void op(px::modrim::CaseInsensitiveStr& value, auto&& ctx, auto&& it, auto&& end) {
        std::string text;
        glz::from<glz::JSON, std::string>::template op<Opts>(text, ctx, it, end);
        value = px::modrim::CaseInsensitiveStr(QString::fromStdString(text));
    }
};
