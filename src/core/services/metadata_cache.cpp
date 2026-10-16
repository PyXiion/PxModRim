#include "core/services/metadata_cache.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <spdlog/spdlog.h>

#include "core/migrator.h"
#include "core/models/metadata/structures.h"

namespace px::modrim::services {

using providers::CacheLookup;
using providers::CachedMod;
using providers::ModCandidate;

namespace {

constexpr int kSchemaVersion = 2;
constexpr qsizetype kFetchChunk = 500;
constexpr double kMtimeTolerance = 1e-4;
constexpr int kMaxSymlinkHops = 40;

constexpr char kSchema[] = R"sql(
    CREATE TABLE IF NOT EXISTS mod_metadata (
        mod_path        TEXT PRIMARY KEY,
        about_path      TEXT NOT NULL,
        mtime           REAL NOT NULL,
        file_size       INTEGER NOT NULL DEFAULT 0,
        target_version  TEXT NOT NULL,
        payload_json    TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_mm_about
        ON mod_metadata(about_path);
)sql";

Result<void> exec(QSqlDatabase& db, const QString& sql) {
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        return std::unexpected(Error{query.lastError().text()});
    }
    return {};
}

// ---- Path normalization -------------------------------------------------------------------

QString join_path(const QString& dir, const QString& name) {
    return dir.endsWith(u'/') ? dir + name : dir + u'/' + name;
}

// "/" for POSIX paths, "C:/" for drive paths.
QString root_of(const QString& absolute) {
    if (absolute.size() >= 3 && absolute[0].isLetter() && absolute[1] == u':' &&
        absolute[2] == u'/') {
        return absolute.left(3);
    }
    return QStringLiteral("/");
}

QString parent_of(const QString& resolved, const QString& root) {
    if (resolved.size() <= root.size()) {
        return root;
    }
    const QString parent = resolved.left(resolved.lastIndexOf(u'/'));
    return parent.size() < root.size() ? root : parent;
}

// Resolves symlinks component by component, consuming ".." after the link target has been
// followed, as Python's posixpath.realpath does.
QString resolve_path(const QString& path) {
    QString absolute = QDir::fromNativeSeparators(path);
    if (!QDir::isAbsolutePath(absolute)) {
        absolute = join_path(QDir::currentPath(), absolute);
    }
    const QString root = root_of(absolute);
    QStringList pending = absolute.mid(root.size()).split(u'/', Qt::SkipEmptyParts);
    QString resolved = root;
    int hops = 0;
    while (!pending.isEmpty()) {
        const QString name = pending.takeFirst();
        if (name == QLatin1String(".")) {
            continue;
        }
        if (name == QLatin1String("..")) {
            resolved = parent_of(resolved, root);
            continue;
        }
        const QString candidate = join_path(resolved, name);
        if (!QFileInfo(candidate).isSymLink()) {
            resolved = candidate;
            continue;
        }
        if (++hops > kMaxSymlinkHops) {
            return candidate;
        }
        const QString target = QDir::fromNativeSeparators(QFile::symLinkTarget(candidate));
        if (QDir::isAbsolutePath(target)) {
            resolved = root;
            pending = target.mid(root_of(target).size()).split(u'/', Qt::SkipEmptyParts) + pending;
        } else {
            pending = target.split(u'/', Qt::SkipEmptyParts) + pending;
        }
    }
    return resolved;
}

// ---- Payload (de)serialization ------------------------------------------------------------

QJsonArray sorted_array(const QStringList& values) {
    QStringList sorted = values;
    sorted.sort();
    return QJsonArray::fromStringList(sorted);
}

template <class Set>
QStringList sorted_strings(const Set& set) {
    QStringList values;
    values.reserve(set.size());
    for (const auto& item : set) {
        values.append(item.str());
    }
    values.sort();
    return values;
}

QJsonObject serialize_mod(const AboutXmlMod& mod) {
    QJsonObject object;
    object[QStringLiteral("name")] = mod.name;
    object[QStringLiteral("package_id")] = mod.package_id.str();
    object[QStringLiteral("authors")] = QJsonArray::fromStringList(mod.authors);
    object[QStringLiteral("mod_version")] = mod.mod_version;
    object[QStringLiteral("mod_icon_path")] =
        mod.mod_icon_path ? QJsonValue(*mod.mod_icon_path) : QJsonValue(QJsonValue::Null);
    object[QStringLiteral("steam_app_id")] = QJsonValue(static_cast<qint64>(mod.steam_app_id));
    object[QStringLiteral("url")] = mod.url;
    object[QStringLiteral("description")] = mod.description;
    QStringList versions = mod.supported_versions.values();
    versions.sort();
    object[QStringLiteral("supported_versions")] = QJsonArray::fromStringList(versions);
    object[QStringLiteral("valid")] = mod.valid;
    object[QStringLiteral("mtime")] = mod.mtime;

    // Declaration order matters to the checker, which QJsonObject would sort away, so
    // dependencies are an array here. deserialize_mod still reads the legacy object form.
    QJsonArray dependencies;
    for (const DependencyMod& dep : mod.about_rules.dependencies) {
        QJsonObject entry;
        entry[QStringLiteral("name")] = dep.name;
        entry[QStringLiteral("package_id")] = dep.package_id.str();
        entry[QStringLiteral("workshop_url")] = dep.workshop_url;
        entry[QStringLiteral("alternative_package_ids")] =
            sorted_array(sorted_strings(dep.alternative_package_ids));
        dependencies.append(entry);
    }
    QJsonObject rules;
    rules[QStringLiteral("load_before")] = sorted_array(sorted_strings(mod.about_rules.load_before));
    rules[QStringLiteral("load_after")] = sorted_array(sorted_strings(mod.about_rules.load_after));
    rules[QStringLiteral("incompatible_with")] =
        sorted_array(sorted_strings(mod.about_rules.incompatible_with));
    rules[QStringLiteral("dependencies")] = dependencies;
    object[QStringLiteral("about_rules")] = rules;
    return object;
}

QString string_or(const QJsonObject& object, const char* key, const QString& fallback) {
    const QJsonValue value = object.value(QLatin1StringView(key));
    return value.isString() ? value.toString() : fallback;
}

QStringList string_list(const QJsonValue& value) {
    QStringList values;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            values.append(item.toString());
        }
    }
    return values;
}

void add_dependency(BaseRules& rules, const QJsonObject& data, const QString& fallback_pid) {
    DependencyMod dep;
    dep.package_id = CaseInsensitiveStr(string_or(data, "package_id", fallback_pid));
    dep.name = string_or(data, "name", QString());
    dep.workshop_url = string_or(data, "workshop_url", QString());
    dep.alternative_package_ids = CaseInsensitiveSet(string_list(data.value(QLatin1StringView("alternative_package_ids"))));
    rules.set_dependency(std::move(dep));
}

void read_dependencies(BaseRules& rules, const QJsonValue& value) {
    if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            if (item.isObject()) {
                add_dependency(rules, item.toObject(), QStringLiteral("invalid.mod"));
            }
        }
    } else if (value.isObject()) {
        const QJsonObject legacy = value.toObject();
        for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
            if (it.value().isObject()) {
                add_dependency(rules, it.value().toObject(), it.key());
            }
        }
    }
}

// The payload as a fresh AboutXmlMod, without a mod path; the caller sets it.
Result<std::shared_ptr<AboutXmlMod>> deserialize_mod(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (document.isNull() || !document.isObject()) {
        return std::unexpected(Error{QStringLiteral("payload is not a JSON object")});
    }
    const QJsonObject data = document.object();

    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = string_or(data, "name", QStringLiteral("Unknown Mod Name"));
    mod->package_id = CaseInsensitiveStr(string_or(data, "package_id", QStringLiteral("invalid.mod")));
    mod->authors = string_list(data.value(QLatin1StringView("authors")));
    mod->mod_version = string_or(data, "mod_version", QString());
    const QString icon = string_or(data, "mod_icon_path", QString());
    if (!icon.isEmpty()) {
        mod->mod_icon_path = icon;
    }
    mod->steam_app_id = data.value(QLatin1StringView("steam_app_id")).toInteger(-1);
    mod->url = string_or(data, "url", QString());
    mod->description = string_or(data, "description", QString());
    const QStringList versions = string_list(data.value(QLatin1StringView("supported_versions")));
    mod->supported_versions = QSet<QString>(versions.begin(), versions.end());
    mod->valid = data.value(QLatin1StringView("valid")).toBool(true);
    mod->mtime = data.value(QLatin1StringView("mtime")).toDouble(0.0);

    const QJsonValue rules_value = data.value(QLatin1StringView("about_rules"));
    const QJsonObject rules_data = rules_value.isObject() ? rules_value.toObject() : QJsonObject{};
    mod->about_rules.load_before = CaseInsensitiveSet(string_list(rules_data.value(QLatin1StringView("load_before"))));
    mod->about_rules.load_after = CaseInsensitiveSet(string_list(rules_data.value(QLatin1StringView("load_after"))));
    mod->about_rules.incompatible_with =
        CaseInsensitiveSet(string_list(rules_data.value(QLatin1StringView("incompatible_with"))));
    read_dependencies(mod->about_rules, rules_data.value(QLatin1StringView("dependencies")));
    return mod;
}

// ---- Batch bookkeeping --------------------------------------------------------------------

// Candidates keyed by normalized mod path. Like Python's dict, a repeated key keeps its first
// position and takes the later value.
struct NormalizedCandidates {
    QList<std::pair<QString, ModCandidate>> entries;
    QHash<QString, qsizetype> index;

    void insert(QString key, const ModCandidate& candidate) {
        if (const auto found = index.constFind(key); found != index.constEnd()) {
            entries[*found].second = candidate;
            return;
        }
        index.insert(key, entries.size());
        entries.append({std::move(key), candidate});
    }
};

NormalizedCandidates normalize_candidates(const QList<ModCandidate>& candidates) {
    QStringList mod_paths;
    mod_paths.reserve(candidates.size());
    for (const ModCandidate& candidate : candidates) {
        mod_paths.append(candidate.modPath);
    }
    const QStringList normalized = normalize_paths(mod_paths);
    NormalizedCandidates result;
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        result.insert(normalized[i], candidates[i]);
    }
    return result;
}

struct StoredRow {
    QString mod_path;
    double mtime = 0.0;
    qint64 file_size = 0;
    QString target_version;
    QString payload;
};

Result<QList<StoredRow>> fetch_rows(QSqlDatabase& db, const QStringList& norm_paths) {
    QList<StoredRow> rows;
    for (qsizetype start = 0; start < norm_paths.size(); start += kFetchChunk) {
        const qsizetype count = std::min(kFetchChunk, norm_paths.size() - start);
        QStringList placeholders(count, QStringLiteral("?"));
        QSqlQuery query(db);
        const QString sql = QStringLiteral("SELECT mod_path, about_path, mtime, file_size, "
                                           "target_version, payload_json FROM mod_metadata "
                                           "WHERE mod_path IN (%1)")
                                .arg(placeholders.join(QStringLiteral(",")));
        if (!query.prepare(sql)) {
            return std::unexpected(Error{query.lastError().text()});
        }
        for (qsizetype i = 0; i < count; ++i) {
            query.addBindValue(norm_paths[start + i]);
        }
        if (!query.exec()) {
            return std::unexpected(Error{query.lastError().text()});
        }
        while (query.next()) {
            rows.append(StoredRow{query.value(0).toString(), query.value(2).toDouble(),
                                  query.value(3).toLongLong(), query.value(4).toString(),
                                  query.value(5).toString()});
        }
    }
    return rows;
}

}  // namespace

QString metadata_cache_path(const QString& config_dir) {
    return QDir(config_dir).filePath(QStringLiteral("metadata-cache.db"));
}

QString normalize_path(const QString& path) {
    return resolve_path(path);
}

QStringList normalize_paths(const QStringList& paths) {
    QHash<QString, QString> resolved_parents;
    QStringList out;
    out.reserve(paths.size());
    for (const QString& raw : paths) {
        const QString path = QDir::fromNativeSeparators(raw);
        const qsizetype slash = path.lastIndexOf(u'/');
        const QString tail = slash < 0 ? QString() : path.mid(slash + 1);
        if (slash < 0 || tail.isEmpty() || tail == QLatin1String(".") || tail == QLatin1String("..") ||
            QFileInfo(raw).isSymLink()) {
            out.append(resolve_path(path));
            continue;
        }
        const QString head = slash == 0 ? QStringLiteral("/") : path.left(slash);
        auto memo = resolved_parents.constFind(head);
        if (memo == resolved_parents.constEnd()) {
            memo = resolved_parents.insert(head, resolve_path(head));
        }
        out.append(join_path(*memo, tail));
    }
    return out;
}

MetadataCache::MetadataCache(QString path)
    : path_(std::move(path)),
      connection_name_(QStringLiteral("px-metadata-cache-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {
    worker_.setMaxThreadCount(1);
    // Keeps the single worker alive between calls, so the connection stays on one thread.
    worker_.setExpiryTimeout(-1);
}

MetadataCache::~MetadataCache() {
    providers::runOnPool<void>(&worker_, [this] { close_connection(); }).waitForFinished();
}

QCoro::Task<CacheLookup> MetadataCache::get_mods(QList<ModCandidate> candidates,
                                                 QString target_version) {
    if (candidates.isEmpty()) {
        co_return CacheLookup{};
    }
    co_return co_await providers::runOnPool<CacheLookup>(
        &worker_, [this, candidates = std::move(candidates),
                   target_version = std::move(target_version)] {
            return lookup(candidates, target_version);
        });
}

QCoro::Task<void> MetadataCache::put_mods(QList<CachedMod> entries, QString target_version) {
    if (entries.isEmpty()) {
        co_return;
    }
    co_await providers::runOnPool<void>(
        &worker_, [this, entries = std::move(entries), target_version = std::move(target_version)] {
            store(entries, target_version);
        });
}

QCoro::Task<void> MetadataCache::prune_disappeared(QSet<QString> live_mod_paths) {
    co_await providers::runOnPool<void>(
        &worker_, [this, live = std::move(live_mod_paths)] { prune(live); });
}

QCoro::Task<void> MetadataCache::close() {
    co_await providers::runOnPool<void>(&worker_, [this] { close_connection(); });
}

void MetadataCache::close_connection() {
    if (connection_) {
        connection_->close();
        connection_.reset();
        QSqlDatabase::removeDatabase(connection_name_);
    }
}

QSqlDatabase* MetadataCache::open_connection() {
    if (disabled_) {
        return nullptr;
    }
    if (connection_) {
        return &*connection_;
    }

    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
        spdlog::warn("Metadata cache unavailable at {}: cannot create its directory. Disabling cache.",
                     path_.toStdString());
        disabled_ = true;
        return nullptr;
    }
    if (auto opened = open_database(); !opened) {
        spdlog::warn("Corrupt metadata cache database at {}: {}. Attempting recovery.",
                     path_.toStdString(), opened.error().message.toStdString());
        remove_database_files();
        if (auto reopened = open_database(); !reopened) {
            spdlog::error("Failed to re-initialize metadata cache at {}: {}. Disabling cache.",
                          path_.toStdString(), reopened.error().message.toStdString());
            disabled_ = true;
            return nullptr;
        }
    }
    return &*connection_;
}

Result<void> MetadataCache::open_database() {
    Result<void> ready;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name_);
        db.setDatabaseName(path_);
        if (db.open()) {
            ready = Result<void>{};
        } else {
            ready = std::unexpected(Error{db.lastError().text()});
        }
        for (const char* pragma : {"PRAGMA journal_mode=WAL", "PRAGMA busy_timeout=5000",
                                   "PRAGMA foreign_keys=ON", "PRAGMA auto_vacuum=INCREMENTAL"}) {
            if (!ready) {
                break;
            }
            ready = exec(db, QString::fromLatin1(pragma));
        }
        if (ready) {
            MigrationSteps steps;
            steps[kSchemaVersion] = [&db] { return exec(db, QStringLiteral("DELETE FROM mod_metadata")); };
            ready = ensure_schema(db, QString::fromLatin1(kSchema), kSchemaVersion, steps, path_,
                                  QStringLiteral("metadata cache schema"));
        }
        if (ready) {
            connection_ = db;
            return {};
        }
        db.close();
    }
    // Dropped only once no handle to the connection remains.
    QSqlDatabase::removeDatabase(connection_name_);
    return ready;
}

void MetadataCache::remove_database_files() const {
    for (const QString& file : {path_, path_ + QStringLiteral("-wal"), path_ + QStringLiteral("-shm")}) {
        QFile::remove(file);
    }
}

CacheLookup MetadataCache::lookup(const QList<ModCandidate>& candidates,
                                  const QString& target_version) {
    QSqlDatabase* db = open_connection();
    if (!db) {
        return CacheLookup{{}, candidates};
    }

    const NormalizedCandidates normalized = normalize_candidates(candidates);
    QStringList keys;
    keys.reserve(normalized.entries.size());
    for (const auto& entry : normalized.entries) {
        keys.append(entry.first);
    }

    auto rows = fetch_rows(*db, keys);
    if (!rows) {
        spdlog::warn("Error reading metadata cache: {}", rows.error().message.toStdString());
        return CacheLookup{{}, candidates};
    }

    CacheLookup result;
    QSet<QString> found;
    for (const StoredRow& row : *rows) {
        const auto index = normalized.index.constFind(row.mod_path);
        if (index == normalized.index.constEnd()) {
            continue;
        }
        const ModCandidate& candidate = normalized.entries[*index].second;
        if (std::abs(row.mtime - candidate.mtime) >= kMtimeTolerance ||
            row.file_size != candidate.size || row.target_version != target_version) {
            continue;
        }
        auto mod = deserialize_mod(row.payload);
        if (!mod) {
            spdlog::warn("Corrupted metadata cache payload for {}: {}", row.mod_path.toStdString(),
                         mod.error().message.toStdString());
            continue;
        }
        if (auto set = (*mod)->set_mod_path(candidate.modPath); !set) {
            continue;
        }
        result.cached.append(*mod);
        found.insert(row.mod_path);
    }
    for (const auto& entry : normalized.entries) {
        if (!found.contains(entry.first)) {
            result.missing.append(entry.second);
        }
    }
    return result;
}

void MetadataCache::store(const QList<CachedMod>& entries, const QString& target_version) {
    QSqlDatabase* db = open_connection();
    if (!db) {
        return;
    }

    struct Row {
        QString mod_path;
        QString about_path;
        double mtime;
        qint64 file_size;
        QString payload;
    };
    QList<Row> rows;
    for (const CachedMod& entry : entries) {
        if (!entry.mod->mod_path()) {
            continue;
        }
        const QJsonDocument document(serialize_mod(*entry.mod));
        rows.append(Row{normalize_path(*entry.mod->mod_path()),
                        normalize_path(entry.candidate.aboutXmlPath), entry.candidate.mtime,
                        entry.candidate.size, QString::fromUtf8(document.toJson(QJsonDocument::Compact))});
    }

    QSqlQuery query(*db);
    if (!query.prepare(QStringLiteral("INSERT OR REPLACE INTO mod_metadata "
                                      "(mod_path, about_path, mtime, file_size, target_version, "
                                      "payload_json) VALUES (?, ?, ?, ?, ?, ?)"))) {
        spdlog::warn("Error writing to metadata cache: {}", query.lastError().text().toStdString());
        return;
    }
    db->transaction();
    for (const Row& row : rows) {
        query.addBindValue(row.mod_path);
        query.addBindValue(row.about_path);
        query.addBindValue(row.mtime);
        query.addBindValue(row.file_size);
        query.addBindValue(target_version);
        query.addBindValue(row.payload);
        if (!query.exec()) {
            spdlog::warn("Error writing to metadata cache: {}", query.lastError().text().toStdString());
            db->rollback();
            return;
        }
    }
    if (!db->commit()) {
        spdlog::warn("Error writing to metadata cache: {}", db->lastError().text().toStdString());
    }
}

void MetadataCache::prune(const QSet<QString>& live_mod_paths) {
    QSqlDatabase* db = open_connection();
    if (!db) {
        return;
    }

    QStringList cached;
    {
        QSqlQuery query(*db);
        if (!query.exec(QStringLiteral("SELECT mod_path FROM mod_metadata"))) {
            spdlog::warn("Error pruning metadata cache: {}", query.lastError().text().toStdString());
            return;
        }
        while (query.next()) {
            cached.append(query.value(0).toString());
        }
    }

    QStringList disappeared;
    for (const QString& path : cached) {
        if (!live_mod_paths.contains(path)) {
            disappeared.append(path);
        }
    }
    if (disappeared.isEmpty()) {
        return;
    }
    spdlog::debug("Pruning {} disappeared mods from metadata cache", disappeared.size());

    QSqlQuery query(*db);
    if (!query.prepare(QStringLiteral("DELETE FROM mod_metadata WHERE mod_path = ?"))) {
        spdlog::warn("Error pruning metadata cache: {}", query.lastError().text().toStdString());
        return;
    }
    db->transaction();
    for (const QString& path : disappeared) {
        query.addBindValue(path);
        if (!query.exec()) {
            spdlog::warn("Error pruning metadata cache: {}", query.lastError().text().toStdString());
            db->rollback();
            return;
        }
    }
    if (!db->commit()) {
        spdlog::warn("Error pruning metadata cache: {}", db->lastError().text().toStdString());
    }
}

}  // namespace px::modrim::services
