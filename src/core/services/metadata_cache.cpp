#include "core/services/metadata_cache.hpp"

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

#include "core/migrator.hpp"
#include "core/models/metadata/structures.hpp"

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

QString joinPath(const QString& dir, const QString& name) {
    return dir.endsWith(u'/') ? dir + name : dir + u'/' + name;
}

// "/" for POSIX paths, "C:/" for drive paths.
QString rootOf(const QString& absolute) {
    if (absolute.size() >= 3 && absolute[0].isLetter() && absolute[1] == u':' &&
        absolute[2] == u'/') {
        return absolute.left(3);
    }
    return QStringLiteral("/");
}

QString parentOf(const QString& resolved, const QString& root) {
    if (resolved.size() <= root.size()) {
        return root;
    }
    const QString parent = resolved.left(resolved.lastIndexOf(u'/'));
    return parent.size() < root.size() ? root : parent;
}

// Resolves symlinks component by component, consuming ".." after the link target has been
// followed, as Python's posixpath.realpath does.
QString resolvePath(const QString& path) {
    QString absolute = QDir::fromNativeSeparators(path);
    if (!QDir::isAbsolutePath(absolute)) {
        absolute = joinPath(QDir::currentPath(), absolute);
    }
    const QString root = rootOf(absolute);
    QStringList pending = absolute.mid(root.size()).split(u'/', Qt::SkipEmptyParts);
    QString resolved = root;
    int hops = 0;
    while (!pending.isEmpty()) {
        const QString name = pending.takeFirst();
        if (name == QLatin1String(".")) {
            continue;
        }
        if (name == QLatin1String("..")) {
            resolved = parentOf(resolved, root);
            continue;
        }
        const QString candidate = joinPath(resolved, name);
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
            pending = target.mid(rootOf(target).size()).split(u'/', Qt::SkipEmptyParts) + pending;
        } else {
            pending = target.split(u'/', Qt::SkipEmptyParts) + pending;
        }
    }
    return resolved;
}

// ---- Payload (de)serialization ------------------------------------------------------------

QJsonArray sortedArray(const QStringList& values) {
    QStringList sorted = values;
    sorted.sort();
    return QJsonArray::fromStringList(sorted);
}

template <class Set>
QStringList sortedStrings(const Set& set) {
    QStringList values;
    values.reserve(set.size());
    for (const auto& item : set) {
        values.append(item.str());
    }
    values.sort();
    return values;
}

QJsonObject serializeMod(const AboutXmlMod& mod) {
    QJsonObject object;
    object[QStringLiteral("name")] = mod.name;
    object[QStringLiteral("package_id")] = mod.packageId.str();
    object[QStringLiteral("authors")] = QJsonArray::fromStringList(mod.authors);
    object[QStringLiteral("mod_version")] = mod.modVersion;
    object[QStringLiteral("mod_icon_path")] =
        mod.modIconPath ? QJsonValue(*mod.modIconPath) : QJsonValue(QJsonValue::Null);
    object[QStringLiteral("steam_app_id")] = QJsonValue(static_cast<qint64>(mod.steamAppId));
    object[QStringLiteral("url")] = mod.url;
    object[QStringLiteral("description")] = mod.description;
    QStringList versions = mod.supportedVersions.values();
    versions.sort();
    object[QStringLiteral("supported_versions")] = QJsonArray::fromStringList(versions);
    object[QStringLiteral("valid")] = mod.valid;
    object[QStringLiteral("mtime")] = mod.mtime;

    // Declaration order matters to the checker, which QJsonObject would sort away, so
    // dependencies are an array here. deserialize_mod still reads the legacy object form.
    QJsonArray dependencies;
    for (const DependencyMod& dep : mod.aboutRules.dependencies) {
        QJsonObject entry;
        entry[QStringLiteral("name")] = dep.name;
        entry[QStringLiteral("package_id")] = dep.packageId.str();
        entry[QStringLiteral("workshop_url")] = dep.workshopUrl;
        entry[QStringLiteral("alternative_package_ids")] =
            sortedArray(sortedStrings(dep.alternativePackageIds));
        dependencies.append(entry);
    }
    QJsonObject rules;
    rules[QStringLiteral("load_before")] = sortedArray(sortedStrings(mod.aboutRules.loadBefore));
    rules[QStringLiteral("load_after")] = sortedArray(sortedStrings(mod.aboutRules.loadAfter));
    rules[QStringLiteral("incompatible_with")] =
        sortedArray(sortedStrings(mod.aboutRules.incompatibleWith));
    rules[QStringLiteral("dependencies")] = dependencies;
    object[QStringLiteral("about_rules")] = rules;
    return object;
}

QString stringOr(const QJsonObject& object, const char* key, const QString& fallback) {
    const QJsonValue value = object.value(QLatin1StringView(key));
    return value.isString() ? value.toString() : fallback;
}

QStringList stringList(const QJsonValue& value) {
    QStringList values;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            values.append(item.toString());
        }
    }
    return values;
}

void addDependency(BaseRules& rules, const QJsonObject& data, const QString& fallbackPid) {
    DependencyMod dep;
    dep.packageId = CaseInsensitiveStr(stringOr(data, "package_id", fallbackPid));
    dep.name = stringOr(data, "name", QString());
    dep.workshopUrl = stringOr(data, "workshop_url", QString());
    dep.alternativePackageIds = CaseInsensitiveSet(stringList(data.value(QLatin1StringView("alternative_package_ids"))));
    rules.setDependency(std::move(dep));
}

void readDependencies(BaseRules& rules, const QJsonValue& value) {
    if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            if (item.isObject()) {
                addDependency(rules, item.toObject(), QStringLiteral("invalid.mod"));
            }
        }
    } else if (value.isObject()) {
        const QJsonObject legacy = value.toObject();
        for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) {
            if (it.value().isObject()) {
                addDependency(rules, it.value().toObject(), it.key());
            }
        }
    }
}

// The payload as a fresh AboutXmlMod, without a mod path; the caller sets it.
Result<std::shared_ptr<AboutXmlMod>> deserializeMod(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (document.isNull() || !document.isObject()) {
        return std::unexpected(Error{QStringLiteral("payload is not a JSON object")});
    }
    const QJsonObject data = document.object();

    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = stringOr(data, "name", QStringLiteral("Unknown Mod Name"));
    mod->packageId = CaseInsensitiveStr(stringOr(data, "package_id", QStringLiteral("invalid.mod")));
    mod->authors = stringList(data.value(QLatin1StringView("authors")));
    mod->modVersion = stringOr(data, "mod_version", QString());
    const QString icon = stringOr(data, "mod_icon_path", QString());
    if (!icon.isEmpty()) {
        mod->modIconPath = icon;
    }
    mod->steamAppId = data.value(QLatin1StringView("steam_app_id")).toInteger(-1);
    mod->url = stringOr(data, "url", QString());
    mod->description = stringOr(data, "description", QString());
    const QStringList versions = stringList(data.value(QLatin1StringView("supported_versions")));
    mod->supportedVersions = QSet<QString>(versions.begin(), versions.end());
    mod->valid = data.value(QLatin1StringView("valid")).toBool(true);
    mod->mtime = data.value(QLatin1StringView("mtime")).toDouble(0.0);

    const QJsonValue rulesValue = data.value(QLatin1StringView("about_rules"));
    const QJsonObject rulesData = rulesValue.isObject() ? rulesValue.toObject() : QJsonObject{};
    mod->aboutRules.loadBefore = CaseInsensitiveSet(stringList(rulesData.value(QLatin1StringView("load_before"))));
    mod->aboutRules.loadAfter = CaseInsensitiveSet(stringList(rulesData.value(QLatin1StringView("load_after"))));
    mod->aboutRules.incompatibleWith =
        CaseInsensitiveSet(stringList(rulesData.value(QLatin1StringView("incompatible_with"))));
    readDependencies(mod->aboutRules, rulesData.value(QLatin1StringView("dependencies")));
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

NormalizedCandidates normalizeCandidates(const QList<ModCandidate>& candidates) {
    QStringList modPaths;
    modPaths.reserve(candidates.size());
    for (const ModCandidate& candidate : candidates) {
        modPaths.append(candidate.modPath);
    }
    const QStringList normalized = normalizePaths(modPaths);
    NormalizedCandidates result;
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        result.insert(normalized[i], candidates[i]);
    }
    return result;
}

struct StoredRow {
    QString modPath;
    double mtime = 0.0;
    qint64 fileSize = 0;
    QString targetVersion;
    QString payload;
};

Result<QList<StoredRow>> fetchRows(QSqlDatabase& db, const QStringList& normPaths) {
    QList<StoredRow> rows;
    for (qsizetype start = 0; start < normPaths.size(); start += kFetchChunk) {
        const qsizetype count = std::min(kFetchChunk, normPaths.size() - start);
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
            query.addBindValue(normPaths[start + i]);
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

QString metadataCachePath(const QString& configDir) {
    return QDir(configDir).filePath(QStringLiteral("metadata-cache.db"));
}

QString normalizePath(const QString& path) {
    return resolvePath(path);
}

QStringList normalizePaths(const QStringList& paths) {
    QHash<QString, QString> resolvedParents;
    QStringList out;
    out.reserve(paths.size());
    for (const QString& raw : paths) {
        const QString path = QDir::fromNativeSeparators(raw);
        const qsizetype slash = path.lastIndexOf(u'/');
        const QString tail = slash < 0 ? QString() : path.mid(slash + 1);
        if (slash < 0 || tail.isEmpty() || tail == QLatin1String(".") || tail == QLatin1String("..") ||
            QFileInfo(raw).isSymLink()) {
            out.append(resolvePath(path));
            continue;
        }
        const QString head = slash == 0 ? QStringLiteral("/") : path.left(slash);
        auto memo = resolvedParents.constFind(head);
        if (memo == resolvedParents.constEnd()) {
            memo = resolvedParents.insert(head, resolvePath(head));
        }
        out.append(joinPath(*memo, tail));
    }
    return out;
}

MetadataCache::MetadataCache(QString path)
    : m_path(std::move(path)),
      m_connectionName(QStringLiteral("px-metadata-cache-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {
    m_worker.setMaxThreadCount(1);
    // Keeps the single worker alive between calls, so the connection stays on one thread.
    m_worker.setExpiryTimeout(-1);
}

MetadataCache::~MetadataCache() {
    providers::runOnPool<void>(&m_worker, [this] { closeConnection(); }).waitForFinished();
}

QCoro::Task<CacheLookup> MetadataCache::getMods(QList<ModCandidate> candidates,
                                                 QString targetVersion) {
    if (candidates.isEmpty()) {
        co_return CacheLookup{};
    }
    co_return co_await providers::runOnPool<CacheLookup>(
        &m_worker, [this, candidates = std::move(candidates),
                   targetVersion = std::move(targetVersion)] {
            return lookup(candidates, targetVersion);
        });
}

QCoro::Task<void> MetadataCache::putMods(QList<CachedMod> entries, QString targetVersion) {
    if (entries.isEmpty()) {
        co_return;
    }
    co_await providers::runOnPool<void>(
        &m_worker, [this, entries = std::move(entries), targetVersion = std::move(targetVersion)] {
            store(entries, targetVersion);
        });
}

QCoro::Task<void> MetadataCache::pruneDisappeared(QSet<QString> liveModPaths) {
    co_await providers::runOnPool<void>(
        &m_worker, [this, live = std::move(liveModPaths)] { prune(live); });
}

QCoro::Task<void> MetadataCache::close() {
    co_await providers::runOnPool<void>(&m_worker, [this] { closeConnection(); });
}

void MetadataCache::closeConnection() {
    if (m_connection) {
        m_connection->close();
        m_connection.reset();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

QSqlDatabase* MetadataCache::openConnection() {
    if (m_disabled) {
        return nullptr;
    }
    if (m_connection) {
        return &*m_connection;
    }

    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        spdlog::warn("Metadata cache unavailable at {}: cannot create its directory. Disabling cache.",
                     m_path.toStdString());
        m_disabled = true;
        return nullptr;
    }
    if (auto opened = openDatabase(); !opened) {
        spdlog::warn("Corrupt metadata cache database at {}: {}. Attempting recovery.",
                     m_path.toStdString(), opened.error().message.toStdString());
        removeDatabaseFiles();
        if (auto reopened = openDatabase(); !reopened) {
            spdlog::error("Failed to re-initialize metadata cache at {}: {}. Disabling cache.",
                          m_path.toStdString(), reopened.error().message.toStdString());
            m_disabled = true;
            return nullptr;
        }
    }
    return &*m_connection;
}

Result<void> MetadataCache::openDatabase() {
    Result<void> ready;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_path);
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
            ready = ensureSchema(db, QString::fromLatin1(kSchema), kSchemaVersion, steps, m_path,
                                  QStringLiteral("metadata cache schema"));
        }
        if (ready) {
            m_connection = db;
            return {};
        }
        db.close();
    }
    // Dropped only once no handle to the connection remains.
    QSqlDatabase::removeDatabase(m_connectionName);
    return ready;
}

void MetadataCache::removeDatabaseFiles() const {
    for (const QString& file : {m_path, m_path + QStringLiteral("-wal"), m_path + QStringLiteral("-shm")}) {
        QFile::remove(file);
    }
}

CacheLookup MetadataCache::lookup(const QList<ModCandidate>& candidates,
                                  const QString& targetVersion) {
    QSqlDatabase* db = openConnection();
    if (!db) {
        return CacheLookup{{}, candidates};
    }

    const NormalizedCandidates normalized = normalizeCandidates(candidates);
    QStringList keys;
    keys.reserve(normalized.entries.size());
    for (const auto& entry : normalized.entries) {
        keys.append(entry.first);
    }

    auto rows = fetchRows(*db, keys);
    if (!rows) {
        spdlog::warn("Error reading metadata cache: {}", rows.error().message.toStdString());
        return CacheLookup{{}, candidates};
    }

    CacheLookup result;
    QSet<QString> found;
    for (const StoredRow& row : *rows) {
        const auto index = normalized.index.constFind(row.modPath);
        if (index == normalized.index.constEnd()) {
            continue;
        }
        const ModCandidate& candidate = normalized.entries[*index].second;
        if (std::abs(row.mtime - candidate.mtime) >= kMtimeTolerance ||
            row.fileSize != candidate.size || row.targetVersion != targetVersion) {
            continue;
        }
        auto mod = deserializeMod(row.payload);
        if (!mod) {
            spdlog::warn("Corrupted metadata cache payload for {}: {}", row.modPath.toStdString(),
                         mod.error().message.toStdString());
            continue;
        }
        if (auto set = (*mod)->setModPath(candidate.modPath); !set) {
            continue;
        }
        result.cached.append(*mod);
        found.insert(row.modPath);
    }
    for (const auto& entry : normalized.entries) {
        if (!found.contains(entry.first)) {
            result.missing.append(entry.second);
        }
    }
    return result;
}

void MetadataCache::store(const QList<CachedMod>& entries, const QString& targetVersion) {
    QSqlDatabase* db = openConnection();
    if (!db) {
        return;
    }

    struct Row {
        QString modPath;
        QString aboutPath;
        double mtime;
        qint64 fileSize;
        QString payload;
    };
    QList<Row> rows;
    for (const CachedMod& entry : entries) {
        if (!entry.mod->modPath()) {
            continue;
        }
        const QJsonDocument document(serializeMod(*entry.mod));
        rows.append(Row{normalizePath(*entry.mod->modPath()),
                        normalizePath(entry.candidate.aboutXmlPath), entry.candidate.mtime,
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
        query.addBindValue(row.modPath);
        query.addBindValue(row.aboutPath);
        query.addBindValue(row.mtime);
        query.addBindValue(row.fileSize);
        query.addBindValue(targetVersion);
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

void MetadataCache::prune(const QSet<QString>& liveModPaths) {
    QSqlDatabase* db = openConnection();
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
        if (!liveModPaths.contains(path)) {
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
