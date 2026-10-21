#if defined(PX_HAS_STEAM) && defined(PX_HAS_WEBENGINE)

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "core/context.hpp"
#include "core/downloads/steam.hpp"
#include "test_support.hpp"
#include "ui/context.hpp"
#include "ui/plugins/steam_workshop/bridge_obj.hpp"
#include "ui/plugins/steam_workshop/plugin.hpp"
#include "ui/plugins/steam_workshop/view.hpp"

namespace {
using namespace px::modrim;

struct PluginFixture {
  QTemporaryDir dir;
  std::unique_ptr<CoreContext> core = px::ui_test::makeCore();
  AppContext app{*core};
  SteamWorkshopUiPlugin plugin;

  PluginFixture() {
    REQUIRE(dir.isValid());
    core->registerPlugin(std::make_unique<SteamDownloader>(SteamDownloaderDeps{
        .manager = core->downloads(),
        .config = core->configService(),
        .localModsPath = [] { return QString{}; },
        .installedMods = [this] { return core->allMods().values(); }}));
    plugin.setup(app);
  }

  QString install(const QString& copy, const QString& publishedId) {
    auto mod = std::make_shared<ListedMod>();
    REQUIRE(mod->setModPath(dir.filePath(copy + QLatin1Char('/') + publishedId))
                .has_value());
    REQUIRE(mod->publishedFileId() == publishedId);
    auto mods = core->allMods();
    const auto uuid = mod->uuid();
    mods.insert(uuid, std::move(mod));
    core->load(std::move(mods), core->activeUuids());
    return uuid;
  }
};
}  // namespace

TEST_CASE("Steam browser accepts only HTTPS Steam Community origins") {
  CHECK(SteamWorkshopViewPanel::isSteamOrigin(
      QUrl(QStringLiteral("https://steamcommunity.com/workshop/"))));
  CHECK(SteamWorkshopViewPanel::isSteamOrigin(
      QUrl(QStringLiteral("https://www.steamcommunity.com/"))));
  CHECK(SteamWorkshopViewPanel::isSteamOrigin(
      QUrl(QStringLiteral("https://HELP.STEAMCOMMUNITY.COM/"))));
  for (const auto* text :
       {"https://evil.com/", "https://steamcommunity.com.evil.com/",
        "https://notsteamcommunity.com/", "http://steamcommunity.com/",
        "https://steamcommunity.com@evil.com/",
        "https://evil.com/?host=steamcommunity.com",
        "https://steampowered.com/", "about:blank"}) {
    CAPTURE(text);
    CHECK_FALSE(
        SteamWorkshopViewPanel::isSteamOrigin(QUrl(QString::fromLatin1(text))));
  }
  CHECK_FALSE(SteamWorkshopViewPanel::isSteamOrigin(QUrl{}));
}

TEST_CASE("Steam bridge rejects calls without a trusted live page") {
  SteamWorkshopUiPlugin plugin;
  PxModRimBridge bridge(plugin, nullptr);
  QSignalSpy replies(&bridge, &PxModRimBridge::resultReady);
  REQUIRE(replies.isValid());
  int sidebarChanges = 0;
  plugin.sidebarSyncRequested.connect(
      [&sidebarChanges](const SidebarSync&) { ++sidebarChanges; });

  const auto call = [&bridge] {
    bridge.call(QStringLiteral("request"),
                QStringLiteral("toggle_download_checked"),
                QStringLiteral(R"({"mod_id":"1","checked":true})"));
  };
  const auto checkDenied = [&] {
    const auto previousReplies = replies.size();
    const auto previousChanges = sidebarChanges;
    call();
    REQUIRE(replies.size() == previousReplies + 1);
    const auto response =
        QJsonDocument::fromJson(replies.back()[1].toString().toUtf8()).object();
    CHECK_FALSE(response.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(response.value(QStringLiteral("error")).toString().isEmpty());
    CHECK(sidebarChanges == previousChanges);
  };

  checkDenied();
  auto page = std::make_unique<QObject>();
  bridge.setPage(page.get());
  page->setProperty("url", QUrl(QStringLiteral("https://steamcommunity.com/")));
  call();
  REQUIRE(replies.size() == 2);
  CHECK(QJsonDocument::fromJson(replies.back()[1].toString().toUtf8())
            .object()
            .value(QStringLiteral("ok"))
            .toBool());
  CHECK(sidebarChanges == 1);

  for (const auto* url :
       {"https://evil.com/", "https://steamcommunity.com.evil.com/",
        "http://steamcommunity.com/"}) {
    page->setProperty("url", QUrl(QString::fromLatin1(url)));
    checkDenied();
  }
  page.reset();
  checkDenied();
}

TEST_CASE("Steam browser deactivation removes every installed copy at once") {
  PluginFixture f;
  const auto first = f.install(QStringLiteral("copy-a"), QStringLiteral("1"));
  const auto duplicate =
      f.install(QStringLiteral("copy-b"), QStringLiteral("1"));
  const auto second = f.install(QStringLiteral("copy-a"), QStringLiteral("2"));
  const auto other = f.install(QStringLiteral("copy-a"), QStringLiteral("3"));
  f.core->setActive({first, duplicate, first, second, other});
  int changes = 0;
  f.core->activeStateChanged().connect(
      [&changes](const QStringList&) { ++changes; });

  SECTION("single published id") {
    f.plugin.toggleActive(QStringLiteral("1"), false);
    CHECK(f.core->activeUuids() == QStringList{second, other});
  }
  SECTION("batch published ids") {
    f.plugin.batchToggleActive({QStringLiteral("1"), QStringLiteral("2")},
                               false);
    CHECK(f.core->activeUuids() == QStringList{other});
  }
  CHECK(changes == 1);
}

TEST_CASE("Steam browser activation keeps one chosen copy per published id") {
  PluginFixture f;
  const auto first = f.install(QStringLiteral("copy-a"), QStringLiteral("1"));
  const auto duplicate =
      f.install(QStringLiteral("copy-b"), QStringLiteral("1"));
  const auto second = f.install(QStringLiteral("copy-a"), QStringLiteral("2"));

  SECTION("single activation chooses only one installed copy") {
    f.plugin.toggleActive(QStringLiteral("1"), true);
    REQUIRE(f.core->activeUuids().size() == 1);
    CHECK((f.core->activeUuids().contains(first) ||
           f.core->activeUuids().contains(duplicate)));
  }
  SECTION("single activation retains an already active copy") {
    f.core->setActive({duplicate});
    f.plugin.toggleActive(QStringLiteral("1"), true);
    CHECK(f.core->activeUuids() == QStringList{duplicate});
  }
  SECTION("batch activation retains an active copy and selects missing ids") {
    f.core->setActive({duplicate});
    int changes = 0;
    f.core->activeStateChanged().connect(
        [&changes](const QStringList&) { ++changes; });
    f.plugin.batchToggleActive({QStringLiteral("1"), QStringLiteral("2"),
                                QStringLiteral("1"), QStringLiteral("missing")},
                               true);
    CHECK(f.core->activeUuids() == QStringList{duplicate, second});
    CHECK(changes == 1);
  }
  SECTION("batch activation chooses one copy for each inactive published id") {
    f.plugin.batchToggleActive({QStringLiteral("1"), QStringLiteral("2")},
                               true);
    REQUIRE(f.core->activeUuids().size() == 2);
    CHECK(f.core->activeUuids().contains(second));
    CHECK((f.core->activeUuids().contains(first) !=
           f.core->activeUuids().contains(duplicate)));
  }
}

#endif
