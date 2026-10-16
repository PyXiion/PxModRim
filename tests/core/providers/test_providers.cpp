#include <catch2/catch_test_macros.hpp>

#include <optional>

#include <QCoreApplication>
#include <QCoro/QCoroTask>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThreadPool>

#include "core/providers/core.hpp"
#include "core/providers/local.hpp"

using namespace px::modrim::providers;
using px::modrim::ListedModPtr;
using px::modrim::Result;

namespace {

QCoreApplication& eventLoop() {
    static int argc = 0;
    static QCoreApplication app(argc, nullptr);
    return app;
}

// Runs a discover() coroutine to completion on the Qt event loop.
template <class T>
T runToCompletion(QCoro::Task<T> task) {
    QCoreApplication& app = eventLoop();
    std::optional<T> result;
    auto driver = [](QCoro::Task<T> inner, std::optional<T>* out) -> QCoro::Task<void> {
        *out = co_await std::move(inner);
        QCoreApplication::quit();
    };
    driver(std::move(task), &result);
    // A discover() that never awaits finishes before the loop starts.
    if (!result) {
        app.exec();
    }
    REQUIRE(result.has_value());
    return *result;
}

// Writes a mod directory with About/About.xml, and optionally About/PublishedFileId.txt.
QString makeMod(const QString& root, const QString& folder, const QString& packageId,
                bool withPublishedFileId) {
    const QString modPath = QDir(root).filePath(folder);
    REQUIRE(QDir(modPath).mkpath(QStringLiteral("About")));

    QFile about(QDir(modPath).filePath(QStringLiteral("About/About.xml")));
    REQUIRE(about.open(QIODevice::WriteOnly));
    about.write(QStringLiteral("<ModMetaData><name>%1</name><packageId>%1</packageId>"
                               "<supportedVersions><li>1.5</li></supportedVersions>"
                               "</ModMetaData>")
                    .arg(packageId)
                    .toUtf8());

    if (withPublishedFileId) {
        QFile pfid(QDir(modPath).filePath(QStringLiteral("About/PublishedFileId.txt")));
        REQUIRE(pfid.open(QIODevice::WriteOnly));
        pfid.write("123456");
    }
    return modPath;
}

// In-memory MetadataCacheStore. `serveCached` makes every candidate a cache hit.
class FakeCache final : public MetadataCacheStore {
public:
    bool serveCached = false;
    ListedModPtr cachedMod;
    int getCalls = 0;
    QList<CachedMod> stored;

    QCoro::Task<px::modrim::providers::CacheLookup> get_mods(
        QList<ModCandidate> candidates, QString /*target_version*/) override {
        ++getCalls;
        px::modrim::providers::CacheLookup lookup;
        if (serveCached) {
            lookup.cached.append(cachedMod);
        } else {
            lookup.missing = candidates;
        }
        co_return lookup;
    }

    QCoro::Task<void> put_mods(QList<CachedMod> entries, QString /*target_version*/) override {
        stored.append(entries);
        co_return;
    }
};

}  // namespace

TEST_CASE("workshop provider discovers mods that carry a published file id") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString workshop = root.filePath(QStringLiteral("workshop"));
    const QString modPath = makeMod(workshop, QStringLiteral("123456"),
                                    QStringLiteral("author.workshop"), true);

    SteamWorkshopModProvider provider(workshop);
    CHECK(provider.provider_id() == QStringLiteral("steam"));
    CHECK(provider.path() == workshop);

    const auto discovered = runToCompletion(provider.discover(QStringLiteral("1.5")));
    REQUIRE(discovered.has_value());
    REQUIRE(discovered->size() == 1);
    const ListedModPtr mod = discovered->constBegin().value();
    CHECK(mod->mod_path() == modPath);
    CHECK(mod->provider_id == QStringLiteral("steam"));
}

TEST_CASE("local and downloaded providers split mods on the published file id") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString local = root.path();
    const QString plain = makeMod(local, QStringLiteral("plain"), QStringLiteral("local.mod"), false);
    const QString downloaded =
        makeMod(local, QStringLiteral("downloaded"), QStringLiteral("dl.mod"), true);

    LocalModProvider localProvider(local);
    DownloadedModProvider downloadedProvider(local);
    CHECK(localProvider.provider_id() == QStringLiteral("local"));
    CHECK(downloadedProvider.provider_id() == QStringLiteral("downloaded"));

    const auto localMods = runToCompletion(localProvider.discover(QStringLiteral("1.5")));
    REQUIRE(localMods.has_value());
    REQUIRE(localMods->size() == 1);
    CHECK(localMods->contains(plain));

    const auto downloadedMods =
        runToCompletion(downloadedProvider.discover(QStringLiteral("1.5")));
    REQUIRE(downloadedMods.has_value());
    REQUIRE(downloadedMods->size() == 1);
    CHECK(downloadedMods->contains(downloaded));
}

TEST_CASE("providers parse several mods through the thread pool") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    for (const char* folder : {"a", "b", "c"}) {
        makeMod(root.path(), QString::fromUtf8(folder),
                QStringLiteral("pool.%1").arg(QString::fromUtf8(folder)), false);
    }

    QThreadPool pool;
    LocalModProvider provider(root.path(), &pool);
    const auto discovered = runToCompletion(provider.discover(QStringLiteral("1.5")));
    REQUIRE(discovered.has_value());
    CHECK(discovered->size() == 3);
    for (const ListedModPtr& mod : *discovered) {
        CHECK(mod->valid);
    }
}

TEST_CASE("core provider reads mods from the game's Data directory") {
    QTemporaryDir game;
    REQUIRE(game.isValid());
    CoreModProvider provider(game.path());
    CHECK(provider.provider_id() == QStringLiteral("core"));
    CHECK(provider.color() == QStringLiteral("#e67e22"));

    // No Data/ directory: nothing to discover.
    const auto missing = runToCompletion(provider.discover(QStringLiteral("1.5")));
    REQUIRE(missing.has_value());
    CHECK(missing->isEmpty());

    makeMod(QDir(game.path()).filePath(QStringLiteral("Data")), QStringLiteral("ludeon.rimworld"),
            QStringLiteral("ludeon.rimworld"), false);
    const auto discovered = runToCompletion(provider.discover(QStringLiteral("1.5")));
    REQUIRE(discovered.has_value());
    REQUIRE(discovered->size() == 1);
    CHECK(discovered->constBegin().value()->name == QStringLiteral("Core"));
}

TEST_CASE("cached metadata is returned without parsing") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString plain = makeMod(root.path(), QStringLiteral("plain"),
                                  QStringLiteral("cached.mod"), false);

    FakeCache cache;
    cache.serveCached = true;
    cache.cachedMod = std::make_shared<px::modrim::AboutXmlMod>();
    REQUIRE(cache.cachedMod->set_mod_path(plain));

    LocalModProvider provider(root.path(), nullptr, &cache);
    const auto discovered = runToCompletion(provider.discover(QStringLiteral("1.5")));
    REQUIRE(discovered.has_value());
    REQUIRE(discovered->size() == 1);
    CHECK(discovered->constBegin().value() == cache.cachedMod);
    CHECK(cache.cachedMod->provider_id == QStringLiteral("local"));
    CHECK(cache.stored.isEmpty());
}

TEST_CASE("newly parsed About.xml data is stored in the cache") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString plain = makeMod(root.path(), QStringLiteral("plain"),
                                  QStringLiteral("parsed.mod"), false);

    FakeCache cache;
    LocalModProvider provider(root.path(), nullptr, &cache);
    const auto discovered = runToCompletion(provider.discover(QStringLiteral("1.5")));
    REQUIRE(discovered.has_value());
    CHECK(discovered->contains(plain));

    REQUIRE(cache.stored.size() == 1);
    CHECK(cache.stored.first().candidate.modPath == plain);
    CHECK(cache.stored.first().mod->valid);
}

TEST_CASE("force_reparse bypasses the cache lookup") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    makeMod(root.path(), QStringLiteral("plain"), QStringLiteral("forced.mod"), false);

    FakeCache cache;
    cache.serveCached = true;
    cache.cachedMod = std::make_shared<px::modrim::AboutXmlMod>();

    LocalModProvider provider(root.path(), nullptr, &cache);
    const auto discovered = runToCompletion(provider.discover(QStringLiteral("1.5"), nullptr,
                                                             nullptr, /*force_reparse=*/true));
    REQUIRE(discovered.has_value());
    CHECK(cache.getCalls == 0);
    CHECK(discovered->size() == 1);
    CHECK(discovered->constBegin().value() != cache.cachedMod);
}
