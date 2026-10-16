#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "core/models/metadata/parsing.hpp"
#include "core/providers/local.hpp"
#include "core/services/metadata_cache.hpp"
#include "services_support.hpp"

using namespace px::modrim;
using namespace px::modrim::providers;
using namespace px::modrim::services;
using services_test::EventLoop;
using services_test::runAsync;
using services_test::writeText;

namespace {

constexpr auto kAboutV1 = R"(<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Sample Mod One</name>
  <author>TestAuthor</author>
  <packageId>Author.SampleModOne</packageId>
  <supportedVersions>
    <li>1.4</li>
    <li>1.5</li>
  </supportedVersions>
  <description>Initial description</description>
  <modVersion>1.0.0</modVersion>
  <url>https://example.com/mod</url>
  <modIconPath>UI/Icon.png</modIconPath>
  <modDependencies>
    <li>
      <packageId>ludeon.rimworld</packageId>
      <displayName>RimWorld</displayName>
    </li>
  </modDependencies>
</ModMetaData>
)";

// Same length as kAboutV1, so only the mtime tells the two apart.
constexpr auto kAboutSameSize = R"(<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Sample Mod Two</name>
  <author>TestAuthor</author>
  <packageId>Author.SampleModOne</packageId>
  <supportedVersions>
    <li>1.4</li>
    <li>1.5</li>
  </supportedVersions>
  <description>Initial description</description>
  <modVersion>1.0.0</modVersion>
  <url>https://example.com/mod</url>
  <modIconPath>UI/Icon.png</modIconPath>
  <modDependencies>
    <li>
      <packageId>ludeon.rimworld</packageId>
      <displayName>RimWorld</displayName>
    </li>
  </modDependencies>
</ModMetaData>
)";

constexpr auto kAboutTwo = R"(<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Sample Mod Two</name>
  <author>OtherAuthor</author>
  <packageId>Author.SampleModTwo</packageId>
  <supportedVersions>
    <li>1.5</li>
  </supportedVersions>
  <description>Second mod description</description>
</ModMetaData>
)";

// Writes About/About.xml (and optionally About/PublishedFileId.txt) under `modDir`.
QString createMod(const QString& modDir, const char* xml, bool withPublishedFileId = false) {
    REQUIRE(QDir().mkpath(QDir(modDir).filePath(QStringLiteral("About"))));
    REQUIRE(writeText(QDir(modDir).filePath(QStringLiteral("About/About.xml")), xml));
    if (withPublishedFileId) {
        REQUIRE(writeText(QDir(modDir).filePath(QStringLiteral("About/PublishedFileId.txt")),
                           "123456"));
    }
    return modDir;
}

// Stat data the way the providers take it: About.xml mtime in seconds, and its size.
ModCandidate candidateFor(const QString& modDir) {
    const QString about = QDir(modDir).filePath(QStringLiteral("About/About.xml"));
    const QFileInfo info(about);
    return ModCandidate{modDir, about, info.lastModified().toMSecsSinceEpoch() / 1000.0,
                        info.size()};
}

// Parses the mod exactly as the providers do, so cache entries match real ones.
std::shared_ptr<AboutXmlMod> parseAbout(const QString& modDir) {
    auto listed = createListedModFromPath(modDir, QStringLiteral("1.5"), true);
    REQUIRE(listed.has_value());
    auto about = std::dynamic_pointer_cast<AboutXmlMod>(*listed);
    REQUIRE(about != nullptr);
    return about;
}

// Moves About.xml's mtime, keeping the file's contents.
void setAboutMtime(const ModCandidate& candidate, double mtimeSeconds) {
    QFile file(candidate.aboutXmlPath);
    REQUIRE(file.open(QIODevice::ReadWrite));
    REQUIRE(file.setFileTime(QDateTime::fromMSecsSinceEpoch(std::llround(mtimeSeconds * 1000)),
                             QFileDevice::FileModificationTime));
}

// Reads mod_path values with an independent connection, to check what the cache stored.
QStringList storedModPaths(const QString& dbPath) {
    const QString name = QStringLiteral("metadata-cache-test-reader");
    QStringList paths;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(dbPath);
        REQUIRE(db.open());
        QSqlQuery query(db);
        REQUIRE(query.exec(QStringLiteral("SELECT mod_path FROM mod_metadata ORDER BY mod_path")));
        while (query.next()) {
            paths.append(query.value(0).toString());
        }
    }
    QSqlDatabase::removeDatabase(name);
    return paths;
}

}  // namespace

TEST_CASE("cache returns the parsed fields of a stored mod") {
    EventLoop loop;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString modDir = createMod(QDir(root.path()).filePath(QStringLiteral("ModA")), kAboutV1);
    const ModCandidate candidate = candidateFor(modDir);
    const QString dbPath = metadataCachePath(root.path());
    MetadataCache cache(dbPath);

    runAsync(cache.putMods({CachedMod{parseAbout(modDir), candidate}}, QStringLiteral("1.5")));
    const CacheLookup lookup = runAsync(cache.getMods({candidate}, QStringLiteral("1.5")));

    REQUIRE(lookup.cached.size() == 1);
    REQUIRE(lookup.missing.isEmpty());
    const auto mod = std::dynamic_pointer_cast<AboutXmlMod>(lookup.cached[0]);
    REQUIRE(mod != nullptr);
    CHECK(mod->name == QStringLiteral("Sample Mod One"));
    CHECK(mod->packageId.str() == QStringLiteral("author.samplemodone"));
    CHECK(mod->authors == QStringList{QStringLiteral("TestAuthor")});
    CHECK(mod->supportedVersions == QSet<QString>{QStringLiteral("1.4"), QStringLiteral("1.5")});
    CHECK(mod->description == QStringLiteral("Initial description"));
    CHECK(mod->modVersion == QStringLiteral("1.0.0"));
    CHECK(mod->url == QStringLiteral("https://example.com/mod"));
    REQUIRE(mod->modIconPath.has_value());
    CHECK(*mod->modIconPath == QStringLiteral("UI/Icon.png"));
    CHECK(std::abs(mod->mtime - candidate.mtime) < 1e-3);
    CHECK(mod->aboutRules.dependencies.size() == 1);
    CHECK(mod->aboutRules.hasDependency(CaseInsensitiveStr(QStringLiteral("ludeon.rimworld"))));
    REQUIRE(mod->modPath().has_value());
    CHECK(*mod->modPath() == modDir);
}

TEST_CASE("cache entries are misses when the file size, mtime or target version changes") {
    EventLoop loop;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString modDir = createMod(QDir(root.path()).filePath(QStringLiteral("ModA")), kAboutV1);
    const ModCandidate candidate = candidateFor(modDir);
    MetadataCache cache(metadataCachePath(root.path()));
    runAsync(cache.putMods({CachedMod{parseAbout(modDir), candidate}}, QStringLiteral("1.5")));

    ModCandidate resized = candidate;
    resized.size += 1;
    ModCandidate touched = candidate;
    touched.mtime += 5.0;

    for (const ModCandidate& stale : {resized, touched}) {
        const CacheLookup lookup = runAsync(cache.getMods({stale}, QStringLiteral("1.5")));
        CHECK(lookup.cached.isEmpty());
        CHECK(lookup.missing.size() == 1);
    }
    const CacheLookup otherVersion = runAsync(cache.getMods({candidate}, QStringLiteral("1.4")));
    CHECK(otherVersion.cached.isEmpty());
    CHECK(otherVersion.missing.size() == 1);
}

TEST_CASE("pruning removes entries for mods that disappeared") {
    EventLoop loop;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString modA = createMod(QDir(root.path()).filePath(QStringLiteral("ModA")), kAboutV1);
    const QString modB = createMod(QDir(root.path()).filePath(QStringLiteral("ModB")), kAboutTwo);
    const QString dbPath = metadataCachePath(root.path());
    MetadataCache cache(dbPath);
    runAsync(cache.putMods({CachedMod{parseAbout(modA), candidateFor(modA)},
                              CachedMod{parseAbout(modB), candidateFor(modB)}},
                             QStringLiteral("1.5")));
    QStringList expectedPaths{normalizePath(modA), normalizePath(modB)};
    expectedPaths.sort();
    REQUIRE(storedModPaths(dbPath) == expectedPaths);

    runAsync(cache.pruneDisappeared(QSet<QString>{normalizePath(modA)}));

    CHECK(storedModPaths(dbPath) == QStringList{normalizePath(modA)});
    const CacheLookup lookup = runAsync(cache.getMods({candidateFor(modB)}, QStringLiteral("1.5")));
    CHECK(lookup.cached.isEmpty());
}

TEST_CASE("corrupt cache database is recovered and keeps working") {
    EventLoop loop;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString modDir = createMod(QDir(root.path()).filePath(QStringLiteral("ModA")), kAboutV1);
    const ModCandidate candidate = candidateFor(modDir);
    const QString dbPath = metadataCachePath(root.path());
    {
        MetadataCache cache(dbPath);
        runAsync(cache.putMods({CachedMod{parseAbout(modDir), candidate}}, QStringLiteral("1.5")));
        runAsync(cache.close());
    }
    QByteArray garbage;
    for (int i = 0; i < 50; ++i) {
        garbage += "corrupt non-sqlite data";
    }
    REQUIRE(writeText(dbPath, garbage));

    MetadataCache recovered(dbPath);
    const CacheLookup empty = runAsync(recovered.getMods({candidate}, QStringLiteral("1.5")));
    CHECK(empty.cached.isEmpty());
    REQUIRE(empty.missing.size() == 1);

    runAsync(recovered.putMods({CachedMod{parseAbout(modDir), candidate}}, QStringLiteral("1.5")));
    const CacheLookup warm = runAsync(recovered.getMods({candidate}, QStringLiteral("1.5")));
    CHECK(warm.cached.size() == 1);
}

TEST_CASE("providers reuse cached metadata and reparse when the file changes") {
    EventLoop loop;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString modsRoot = QDir(root.path()).filePath(QStringLiteral("mods"));
    const QString modDir = createMod(QDir(modsRoot).filePath(QStringLiteral("ModA")), kAboutV1);
    const QString about = QDir(modDir).filePath(QStringLiteral("About/About.xml"));
    const ModCandidate original = candidateFor(modDir);
    MetadataCache cache(metadataCachePath(root.path()));
    LocalModProvider provider(modsRoot, nullptr, &cache);

    auto first = runAsync(provider.discover(QStringLiteral("1.5")));
    REQUIRE(first.has_value());
    REQUIRE(first->contains(modDir));
    CHECK((*first)[modDir]->name == QStringLiteral("Sample Mod One"));

    // Same size and mtime, different text: the cache still answers, a forced reparse does not.
    REQUIRE(writeText(about, kAboutSameSize));
    setAboutMtime(original, original.mtime);
    auto cached = runAsync(provider.discover(QStringLiteral("1.5")));
    REQUIRE(cached.has_value());
    CHECK((*cached)[modDir]->name == QStringLiteral("Sample Mod One"));
    auto forced = runAsync(provider.discover(QStringLiteral("1.5"), nullptr, nullptr, true));
    REQUIRE(forced.has_value());
    CHECK((*forced)[modDir]->name == QStringLiteral("Sample Mod Two"));

    // A new mtime invalidates the entry even though the cache is consulted.
    setAboutMtime(original, original.mtime + 5.0);
    auto changed = runAsync(provider.discover(QStringLiteral("1.5")));
    REQUIRE(changed.has_value());
    CHECK((*changed)[modDir]->name == QStringLiteral("Sample Mod Two"));
    // mtime lives on AboutXmlMod; ListedMod has no such field.
    const auto changedAbout = std::dynamic_pointer_cast<AboutXmlMod>((*changed)[modDir]);
    REQUIRE(changedAbout != nullptr);
    CHECK(std::abs(changedAbout->mtime - (original.mtime + 5.0)) < 1e-3);
}

TEST_CASE("providers keep their identity when they share one cache") {
    EventLoop loop;
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString localRoot = QDir(root.path()).filePath(QStringLiteral("local"));
    const QString downloadedRoot = QDir(root.path()).filePath(QStringLiteral("downloaded"));
    const QString localMod = createMod(QDir(localRoot).filePath(QStringLiteral("LocalMod")), kAboutV1);
    const QString steamMod = createMod(QDir(downloadedRoot).filePath(QStringLiteral("SteamMod")), kAboutTwo, true);
    MetadataCache cache(metadataCachePath(root.path()));
    LocalModProvider local(localRoot, nullptr, &cache);
    DownloadedModProvider downloaded(downloadedRoot, nullptr, &cache);

    for (int pass = 0; pass < 2; ++pass) {
        auto localMods = runAsync(local.discover(QStringLiteral("1.5")));
        auto steamMods = runAsync(downloaded.discover(QStringLiteral("1.5")));
        REQUIRE(localMods.has_value());
        REQUIRE(steamMods.has_value());
        REQUIRE(localMods->contains(localMod));
        REQUIRE(steamMods->contains(steamMod));
        CHECK((*localMods)[localMod]->providerId == QStringLiteral("local"));
        CHECK((*steamMods)[steamMod]->providerId == QStringLiteral("downloaded"));
    }
}

TEST_CASE("normalize_paths matches normalize_path, including symlinks") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString real = QDir(root.path()).filePath(QStringLiteral("real"));
    const QString other = QDir(root.path()).filePath(QStringLiteral("other"));
    REQUIRE(QDir().mkpath(QDir(real).filePath(QStringLiteral("mod"))));
    REQUIRE(QDir().mkpath(other));
    REQUIRE(QFile::link(real, QDir(root.path()).filePath(QStringLiteral("linkdir"))));
    REQUIRE(QFile::link(other, QDir(real).filePath(QStringLiteral("linkmod"))));

    const QString linkdir = QDir(root.path()).filePath(QStringLiteral("linkdir"));
    const QStringList paths{
        QDir(real).filePath(QStringLiteral("mod")),
        QDir(linkdir).filePath(QStringLiteral("mod")),
        QDir(linkdir).filePath(QStringLiteral("linkmod")),
        QDir(root.path()).filePath(QStringLiteral("linkdir/../real/mod")),
        QDir(root.path()).filePath(QStringLiteral("missing/x")),
        QStringLiteral("relative/mod"),
    };

    QStringList expected;
    for (const QString& path : paths) {
        expected.append(normalizePath(path));
    }
    CHECK(normalizePaths(paths) == expected);
    CHECK(normalizePath(QDir(linkdir).filePath(QStringLiteral("linkmod"))) == normalizePath(other));
}
