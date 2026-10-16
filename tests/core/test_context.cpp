#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "core/config.h"
#include "core/context.h"
#include "core/models/metadata/structures.h"
#include "services/services_support.h"

using px::modrim::AboutXmlMod;
using px::modrim::AppConfig;
using px::modrim::CaseInsensitiveStr;
using px::modrim::CoreContext;
using px::modrim::DependencyMod;
using px::modrim::ListedModPtr;
using services_test::EventLoop;
using services_test::run_async;

namespace {

std::shared_ptr<AboutXmlMod> about(const QString& package_id, bool valid = true) {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = package_id;
    mod->package_id = CaseInsensitiveStr(package_id);
    mod->provider_id = QStringLiteral("local");
    mod->valid = valid;
    return mod;
}

std::unique_ptr<CoreContext> make_context(const QString& game_path = {}) {
    static QTemporaryDir configDir;
    static px::modrim::ConfigService configService(configDir.path());
    AppConfig cfg;
    cfg.paths.game = game_path.toStdString();
    return CoreContext::create(cfg, &configService);
}

void write_version(const QString& game_dir, const QString& version) {
    QFile file(QDir(game_dir).filePath(QStringLiteral("Version.txt")));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(version.toUtf8());
}

}  // namespace

TEST_CASE("target version is the major.minor of the game version") {
    QTemporaryDir game;
    REQUIRE(game.isValid());

    auto ctx = make_context();
    CHECK(ctx->gameVersion() == QStringLiteral("Unknown"));
    CHECK(ctx->targetVersion() == QStringLiteral("Unknown"));

    write_version(game.path(), QStringLiteral("1.5.4297 rev123"));
    ctx->updateConfig([&] {
        AppConfig cfg = ctx->config();
        cfg.paths.game = game.path().toStdString();
        return cfg;
    }());
    CHECK(ctx->gameVersion() == QStringLiteral("1.5.4297 rev123"));
    CHECK(ctx->targetVersion() == QStringLiteral("1.5"));
}

TEST_CASE("a game version without a minor part has no target version") {
    QTemporaryDir game;
    REQUIRE(game.isValid());
    write_version(game.path(), QStringLiteral("1"));

    auto ctx = make_context(game.path());
    CHECK(ctx->gameVersion() == QStringLiteral("1"));
    CHECK(ctx->targetVersion() == QStringLiteral("Unknown"));
}

TEST_CASE("setActive emits on every change and keeps the loaded list") {
    EventLoop loop;
    auto ctx = make_context();
    ctx->load({{QStringLiteral("a"), about(QStringLiteral("a.mod"))},
               {QStringLiteral("b"), about(QStringLiteral("b.mod"))}},
              {QStringLiteral("a")});

    std::vector<QStringList> events;
    const auto id = ctx->activeStateChanged().connect(
        [&events](const QStringList& active) { events.push_back(active); });

    ctx->setActive({QStringLiteral("a"), QStringLiteral("b")});
    REQUIRE(events.size() == 1);
    CHECK(events.back() == QStringList{"a", "b"});

    ctx->setActive({QStringLiteral("a"), QStringLiteral("b")});
    CHECK(events.size() == 1);

    ctx->setActive({QStringLiteral("b"), QStringLiteral("a")});
    REQUIRE(events.size() == 2);
    CHECK(ctx->activeUuids() == QStringList{"b", "a"});
    CHECK(ctx->loadedActiveUuids() == QStringList{"a"});

    CHECK(ctx->activeStateChanged().disconnect(id));
}

TEST_CASE("computeStats counts total, active, inactive and invalid mods") {
    EventLoop loop;
    auto ctx = make_context();
    ctx->load({{QStringLiteral("a"), about(QStringLiteral("a.mod"))},
               {QStringLiteral("b"), about(QStringLiteral("b.mod"))},
               {QStringLiteral("c"), about(QStringLiteral("c.mod"), false)}},
              {QStringLiteral("a")});

    const auto stats = ctx->computeStats();
    CHECK(stats.total == 3);
    CHECK(stats.active == 1);
    CHECK(stats.inactive == 2);
    CHECK(stats.errors == 1);

    const auto override_stats = ctx->computeStats(QStringList{"a", "b"});
    CHECK(override_stats.active == 2);
    CHECK(override_stats.inactive == 1);
}

TEST_CASE("autoSort enables a missing dependency and orders it first") {
    EventLoop loop;
    auto dependent = about(QStringLiteral("mod.a"));
    DependencyMod dependency;
    dependency.name = QStringLiteral("Mod B");
    dependency.package_id = CaseInsensitiveStr(QStringLiteral("mod.b"));
    dependent->about_rules.dependencies.append(std::move(dependency));

    auto ctx = make_context();
    ctx->load({{QStringLiteral("uuid-a"), dependent},
               {QStringLiteral("uuid-b"), about(QStringLiteral("mod.b"))}},
              {QStringLiteral("uuid-a")});

    const auto [count, elapsed_ms] = run_async(ctx->autoSort());
    CHECK(count == 2);
    CHECK(elapsed_ms >= 0.0);
    REQUIRE(ctx->activeUuids().size() == 2);
    CHECK(ctx->activeUuids().indexOf(QStringLiteral("uuid-b")) <
          ctx->activeUuids().indexOf(QStringLiteral("uuid-a")));
}
