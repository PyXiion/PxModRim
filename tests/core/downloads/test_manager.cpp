#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QCoro/QCoroTask>

#include <catch2/catch_test_macros.hpp>

#include "../test_support.hpp"
#include "core/downloads/downloader.hpp"
#include "core/downloads/manager.hpp"
#include "core/downloads/types.hpp"
#include "core/models/metadata/structures.hpp"

namespace {

using px::modrim::Downloader;
using px::modrim::DownloadManager;
using px::modrim::DownloadResult;
using px::modrim::ListedMod;
using px::modrim::ListedModPtr;
using px::modrim::Result;
using test_support::runSync;

// Claims ids that start with `prefix`, and records each batch it was asked to
// download.
class FakeDownloader : public Downloader {
 public:
  FakeDownloader(QString name, QString label, QString prefix,
                 DownloadManager& manager)
      : Downloader(std::move(name), std::move(label), manager),
        m_prefix(std::move(prefix)) {}

  bool isDownloading() const override { return m_busy; }
  QSet<QString> activeIds() const override { return {}; }
  bool accepts(const QString& modId) const override {
    return modId.startsWith(m_prefix);
  }

  std::optional<QString> updatableId(const ListedMod& mod) const override {
    const auto& path = mod.modPath();
    if (!path) {
      return std::nullopt;
    }
    const QString id = QFileInfo(*path).fileName();
    return accepts(id) ? std::optional<QString>(id) : std::nullopt;
  }

  std::optional<double> lastSynced(const ListedMod& mod) const override {
    return updatableId(mod) ? std::optional<double>(1.0) : std::nullopt;
  }

  QCoro::Task<Result<DownloadResult>> downloadMods(
      QStringList modIds) override {
    calls.push_back(modIds);
    m_busy = true;
    busyChanged.notify(true);
    m_busy = false;
    busyChanged.notify(false);
    DownloadResult result{.succeeded = modIds, .failed = {}, .changed = {}};
    downloadFinished.notify(result);
    co_return result;
  }

  void cancel() override { cancelled = true; }

  std::vector<QStringList> calls;
  bool cancelled = false;

 private:
  QString m_prefix;
  bool m_busy = false;
};

class FakeSteam : public FakeDownloader {
 public:
  explicit FakeSteam(DownloadManager& manager)
      : FakeDownloader("steam", "Steam", "s", manager) {}
};

class FakeGithub : public FakeDownloader {
 public:
  explicit FakeGithub(DownloadManager& manager)
      : FakeDownloader("github", "Github", "g", manager) {}
};

ListedModPtr makeMod(const QString& folder) {
  auto mod = std::make_shared<ListedMod>();
  REQUIRE(mod->setModPath(QStringLiteral("/mods/%1").arg(folder)).has_value());
  return mod;
}

}  // namespace

TEST_CASE("empty manager offers nothing", "[downloads][manager]") {
  DownloadManager manager;
  REQUIRE_FALSE(manager.available());
  REQUIRE(manager.updatableIds({makeMod("s1")}).isEmpty());
  REQUIRE(manager.sourceOf(*makeMod("s1")) == nullptr);
}

TEST_CASE("download routes ids to the accepting downloader",
          "[downloads][manager]") {
  DownloadManager manager;
  FakeSteam steam(manager);
  FakeGithub github(manager);
  manager.registerDownloader(steam);
  manager.registerDownloader(github);

  auto result = runSync(manager.downloadMods({"s1", "g1", "s2", "s1"}));

  REQUIRE(result.has_value());
  REQUIRE(steam.calls == std::vector<QStringList>{{"s1", "s2"}});
  REQUIRE(github.calls == std::vector<QStringList>{{"g1"}});
  QStringList succeeded = result->succeeded;
  succeeded.sort();
  REQUIRE(succeeded == QStringList{"g1", "s1", "s2"});
}

TEST_CASE("unclaimed id is rejected before anything starts",
          "[downloads][manager]") {
  DownloadManager manager;
  FakeSteam steam(manager);
  manager.registerDownloader(steam);

  auto result = runSync(manager.downloadMods({"s1", "x1"}));

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().message.contains("x1"));
  REQUIRE(steam.calls.empty());
}

TEST_CASE("source and ids come from the owning downloader",
          "[downloads][manager]") {
  DownloadManager manager;
  FakeSteam steam(manager);
  FakeGithub github(manager);
  manager.registerDownloader(steam);
  manager.registerDownloader(github);
  const auto g1 = makeMod("g1");
  const auto other = makeMod("other");
  const QList<ListedModPtr> mods{makeMod("s1"), g1, other};

  const Downloader* source = manager.sourceOf(*g1);
  REQUIRE(source != nullptr);
  REQUIRE(source->label() == "Github");
  REQUIRE(manager.updatableIds(mods) == QStringList{"s1", "g1"});
  REQUIRE_FALSE(manager.lastSynced(*other).has_value());
}

TEST_CASE("busy reflects any downloader and cancel stops the rest",
          "[downloads][manager]") {
  DownloadManager manager;
  FakeSteam steam(manager);
  FakeGithub github(manager);
  manager.registerDownloader(steam);
  manager.registerDownloader(github);
  std::vector<bool> busy;
  manager.busyChanged.connect(
      [&busy](const bool& value) { busy.push_back(value); });

  steam.downloadFinished.connect(
      [&manager](const DownloadResult&) { manager.cancel(); });
  auto result = runSync(manager.downloadMods({"s1", "g1"}));

  REQUIRE(result.has_value());
  REQUIRE(github.calls.empty());
  REQUIRE(busy == std::vector<bool>{true, false});
}

TEST_CASE("names are keyed by source id, not workshop id",
          "[downloads][manager]") {
  DownloadManager manager;
  FakeGithub github(manager);
  manager.registerDownloader(github);
  const auto mod = makeMod("g1");
  mod->name = QStringLiteral("Git Mod");

  const QHash<QString, QString> names = manager.namesById({mod, makeMod("s1")});

  REQUIRE(names == QHash<QString, QString>{{"g1", "Git Mod"}});
}
