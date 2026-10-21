#include <QDir>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "core/context.hpp"
#include "core/models/metadata/structures.hpp"
#include "services/services_support.hpp"

using px::modrim::AboutXmlMod;
using px::modrim::AppConfig;
using px::modrim::CaseInsensitiveStr;
using px::modrim::CoreContext;
using px::modrim::DependencyMod;
using px::modrim::ListedModPtr;
using services_test::EventLoop;
using services_test::runAsync;

namespace {

std::shared_ptr<AboutXmlMod> about(const QString& packageId) {
  auto mod = std::make_shared<AboutXmlMod>();
  mod->name = packageId;
  mod->packageId = CaseInsensitiveStr(packageId);
  mod->providerId = QStringLiteral("local");
  return mod;
}

std::unique_ptr<CoreContext> makeContext(const QString& gamePath = {}) {
  static QTemporaryDir configDir;
  static px::modrim::ConfigService configService(configDir.path());
  AppConfig cfg;
  cfg.paths.game = gamePath.toStdString();
  return CoreContext::create(cfg, configService);
}

void writeVersion(const QString& gameDir, const QString& version) {
  QFile file(QDir(gameDir).filePath(QStringLiteral("Version.txt")));
  REQUIRE(file.open(QIODevice::WriteOnly));
  file.write(version.toUtf8());
}

}  // namespace

TEST_CASE("target version is the major.minor of the game version") {
  QTemporaryDir game;
  REQUIRE(game.isValid());

  auto ctx = makeContext();
  CHECK(ctx->gameVersion() == QStringLiteral("Unknown"));
  CHECK(ctx->targetVersion() == QStringLiteral("Unknown"));

  writeVersion(game.path(), QStringLiteral("1.5.4297 rev123"));
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
  writeVersion(game.path(), QStringLiteral("1"));

  auto ctx = makeContext(game.path());
  CHECK(ctx->gameVersion() == QStringLiteral("1"));
  CHECK(ctx->targetVersion() == QStringLiteral("Unknown"));
}

TEST_CASE("setActive emits on every change and keeps the loaded list") {
  EventLoop loop;
  auto ctx = makeContext();
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

TEST_CASE("autoSort enables a missing dependency and orders it first") {
  EventLoop loop;
  auto dependent = about(QStringLiteral("mod.a"));
  DependencyMod dependency;
  dependency.name = QStringLiteral("Mod B");
  dependency.packageId = CaseInsensitiveStr(QStringLiteral("mod.b"));
  dependent->aboutRules.dependencies.append(std::move(dependency));

  auto ctx = makeContext();
  ctx->load({{QStringLiteral("uuid-a"), dependent},
             {QStringLiteral("uuid-b"), about(QStringLiteral("mod.b"))}},
            {QStringLiteral("uuid-a")});

  const auto [count, elapsedMs] = runAsync(ctx->autoSort());
  CHECK(count == 2);
  CHECK(elapsedMs >= 0.0);
  REQUIRE(ctx->activeUuids().size() == 2);
  CHECK(ctx->activeUuids().indexOf(QStringLiteral("uuid-b")) <
        ctx->activeUuids().indexOf(QStringLiteral("uuid-a")));
}
