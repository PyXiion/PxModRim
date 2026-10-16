#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "core/context.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/models/view/sidebar.hpp"
#include "core/services/activation_service.hpp"
#include "core/services/diagnostics_service.hpp"
#include "services_support.hpp"

using namespace px::modrim;

namespace {

using Mods = QHash<QString, ListedModPtr>;

QStringList ids(std::initializer_list<const char*> items) {
  QStringList result;
  for (const char* item : items) {
    result.append(QString::fromLatin1(item));
  }
  return result;
}

// About.xml mod with the given rules. Each dependency is a bare package id.
ListedModPtr makeMod(const char* name, const char* pid,
                     std::initializer_list<const char*> deps = {},
                     std::initializer_list<const char*> loadAfter = {},
                     std::initializer_list<const char*> loadBefore = {}) {
  auto mod = std::make_shared<AboutXmlMod>();
  mod->name = QString::fromLatin1(name);
  mod->packageId = CaseInsensitiveStr(QString::fromLatin1(pid));
  mod->providerId = QStringLiteral("stub");
  mod->valid = true;
  mod->aboutRules.loadAfter = CaseInsensitiveSet(ids(loadAfter));
  mod->aboutRules.loadBefore = CaseInsensitiveSet(ids(loadBefore));
  for (const char* dep : deps) {
    DependencyMod dependency;
    dependency.name = QString::fromLatin1(dep);
    dependency.packageId = CaseInsensitiveStr(QString::fromLatin1(dep));
    mod->aboutRules.setDependency(dependency);
  }
  return mod;
}

Mods baseMods() {
  Mods mods;
  mods.insert("uuid-a", makeMod("Mod A", "mod.a"));
  mods.insert("uuid-b", makeMod("Mod B", "mod.b", {"mod.a"}));
  mods.insert("uuid-c", makeMod("Mod C", "mod.c", {"mod.b"}));
  mods.insert("uuid-d", makeMod("Mod D", "mod.d"));
  mods.insert("uuid-e", makeMod("Mod E", "mod.e", {"mod.a"}));
  mods.insert("uuid-f", makeMod("Mod F", "mod.f"));
  mods.insert("uuid-g", makeMod("Mod G", "mod.g", {"mod.f"}));
  mods.insert("uuid-h", makeMod("Mod H", "mod.h", {}, {"mod.b"}, {"mod.a"}));
  mods.insert("uuid-i", makeMod("Mod I", "mod.i", {}, {"mod.d"}));
  mods.insert("uuid-j", makeMod("Mod J", "mod.j", {}, {}, {"mod.a"}));
  return mods;
}

// Owns the config directory and the context for one test. Starts with the base
// mods loaded as c, d, a, b and the diagnostics rebuilt.
struct Fixture {
  services_test::EventLoop loop;
  QTemporaryDir dir;
  ConfigService configService{dir.path()};
  std::unique_ptr<CoreContext> ctx =
      CoreContext::create(AppConfig{}, &configService);

  Fixture() {
    ctx->load(baseMods(), ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b"}));
    ctx->diagnosticsService().rebuild();
  }

  void reload(Mods mods, std::initializer_list<const char*> active) {
    ctx->load(std::move(mods), ids(active));
    ctx->diagnosticsService().rebuild();
  }
};

}  // namespace

TEST_CASE(
    "enabling inserts after dependency without reordering existing mods") {
  Fixture f;
  std::vector<QStringList> emitted;
  f.ctx->activeStateChanged().connect(
      [&emitted](const QStringList& uuids) { emitted.push_back(uuids); });

  REQUIRE(f.ctx->activation().apply(ids({"uuid-f", "uuid-e"})));

  const QStringList expected =
      ids({"uuid-c", "uuid-d", "uuid-a", "uuid-e", "uuid-b", "uuid-f"});
  CHECK(f.ctx->activeUuids() == expected);
  REQUIRE(emitted.size() == 1);
  CHECK(emitted.front() == expected);
}

TEST_CASE("unconstrained enable appends") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply(ids({"uuid-f"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"}));
}

TEST_CASE("enabling mod with load-after rule inserts after target") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply(ids({"uuid-i"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-i", "uuid-a", "uuid-b"}));
}

TEST_CASE("enabling mod with load-before rule inserts before target") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply(ids({"uuid-j"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-j", "uuid-a", "uuid-b"}));
}

TEST_CASE("enabling dependency before active dependent") {
  Fixture f;
  f.reload(baseMods(), {"uuid-b", "uuid-c", "uuid-d"});

  REQUIRE(f.ctx->activation().apply(ids({"uuid-a"})));

  CHECK(f.ctx->activeUuids() == ids({"uuid-a", "uuid-b", "uuid-c", "uuid-d"}));
}

TEST_CASE("conflicting constraints fall back to appending") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply(ids({"uuid-h"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-h"}));
}

TEST_CASE("batch enables respect dependency order") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply(ids({"uuid-g", "uuid-f"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f", "uuid-g"}));
}

TEST_CASE("enabling already active mod keeps its position") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply(ids({"uuid-c", "uuid-f"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"}));
}

TEST_CASE("disabling removes and keeps remaining order") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply({}, ids({"uuid-d", "uuid-a"})));

  CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-b"}));
}

TEST_CASE("unknown ids are ignored") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply(ids({"uuid-missing", "uuid-f"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"}));
}

TEST_CASE("mixed selection applies in a single emission") {
  Fixture f;
  std::vector<QStringList> emitted;
  f.ctx->activeStateChanged().connect(
      [&emitted](const QStringList& uuids) { emitted.push_back(uuids); });

  REQUIRE(f.ctx->activation().apply(ids({"uuid-f", "uuid-e"}),
                                    ids({"uuid-a", "uuid-d"})));

  const QStringList expected = ids({"uuid-c", "uuid-b", "uuid-f", "uuid-e"});
  CHECK(f.ctx->activeUuids() == expected);
  REQUIRE(emitted.size() == 1);
  CHECK(emitted.front() == expected);
}

TEST_CASE("uuid in both enable and disable favors disable") {
  Fixture f;
  std::vector<QStringList> emitted;
  f.ctx->activeStateChanged().connect(
      [&emitted](const QStringList& uuids) { emitted.push_back(uuids); });

  REQUIRE(f.ctx->activation().apply(ids({"uuid-a", "uuid-f", "uuid-e"}),
                                    ids({"uuid-a", "uuid-f"})));

  const QStringList expected = ids({"uuid-c", "uuid-d", "uuid-b", "uuid-e"});
  CHECK(f.ctx->activeUuids() == expected);
  REQUIRE(emitted.size() == 1);
  CHECK(emitted.front() == expected);
}

TEST_CASE("duplicate enable is deduped in input order") {
  Fixture f;

  REQUIRE(f.ctx->activation().apply(ids({"uuid-f", "uuid-e", "uuid-f"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-a", "uuid-e", "uuid-b", "uuid-f"}));
}

TEST_CASE("no-op returns false without emitting") {
  const std::vector<std::pair<QStringList, QStringList>> cases{
      {ids({"uuid-a", "uuid-c"}), {}},
      {{}, ids({"uuid-e", "uuid-f"})},
      {ids({"uuid-missing"}), {}},
      {{}, ids({"uuid-missing"})},
      {{}, {}},
      {ids({"uuid-missing"}), ids({"uuid-missing"})},
  };
  for (const auto& [enable, disable] : cases) {
    Fixture f;
    int emissions = 0;
    f.ctx->activeStateChanged().connect(
        [&emissions](const QStringList&) { ++emissions; });

    CHECK_FALSE(f.ctx->activation().apply(enable, disable));

    CHECK(f.ctx->activeUuids() ==
          ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b"}));
    CHECK(emissions == 0);
  }
}

TEST_CASE("edges through an uninstalled mod add no constraint") {
  Fixture f;
  Mods mods;
  mods.insert("uuid-a", makeMod("Mod A", "mod.a"));
  mods.insert("uuid-w", makeMod("Mod W", "mod.w", {}, {"ce.absent"}));
  mods.insert("uuid-y", makeMod("Mod Y", "mod.y", {}, {}, {"ce.absent"}));
  f.reload(mods, {"uuid-a", "uuid-w"});

  REQUIRE(f.ctx->activation().apply(ids({"uuid-y"})));

  CHECK(f.ctx->activeUuids() == ids({"uuid-a", "uuid-w", "uuid-y"}));
}

TEST_CASE("bounds ignore an inconsistent active order") {
  Fixture f;
  Mods mods = baseMods();
  // Active c depends on b but sits before it; X must still land in d..b.
  mods.insert("uuid-x", makeMod("Mod X", "mod.x", {"mod.d"}, {}, {"mod.b"}));
  f.reload(mods, {"uuid-c", "uuid-d", "uuid-a", "uuid-b"});

  REQUIRE(f.ctx->activation().apply(ids({"uuid-x"})));

  CHECK(f.ctx->activeUuids() ==
        ids({"uuid-c", "uuid-d", "uuid-x", "uuid-a", "uuid-b"}));
}

TEST_CASE("batch cycle does not unplace downstream mods") {
  Fixture f;
  Mods mods = baseMods();
  mods.insert("uuid-p", makeMod("Mod P", "mod.p", {"mod.q"}));
  mods.insert("uuid-q", makeMod("Mod Q", "mod.q", {"mod.p"}));
  mods.insert("uuid-z", makeMod("Mod Z", "mod.z", {"mod.p"}));
  mods.insert("uuid-k", makeMod("Mod K", "mod.k", {"mod.d"}, {}, {"mod.a"}));
  f.reload(mods, {"uuid-c", "uuid-d", "uuid-a", "uuid-b"});

  REQUIRE(
      f.ctx->activation().apply(ids({"uuid-z", "uuid-k", "uuid-q", "uuid-p"})));

  CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-k", "uuid-a",
                                     "uuid-b", "uuid-q", "uuid-p", "uuid-z"}));
}

TEST_CASE("dependents_of lists transitive dependents in active order") {
  Fixture f;

  CHECK(f.ctx->activation().dependentsOf(ids({"uuid-a"})) ==
        ids({"uuid-c", "uuid-b"}));
}

TEST_CASE("dependents_of excludes its inputs") {
  Fixture f;

  CHECK(f.ctx->activation().dependentsOf(ids({"uuid-a", "uuid-b"})) ==
        ids({"uuid-c"}));
}

TEST_CASE("dependents_of excludes inactive dependents") {
  Fixture f;

  CHECK_FALSE(f.ctx->activeUuids().contains(QStringLiteral("uuid-e")));
  CHECK_FALSE(f.ctx->activation()
                  .dependentsOf(ids({"uuid-a"}))
                  .contains(QStringLiteral("uuid-e")));
}

TEST_CASE("dependents_of reflects the graph after a state change") {
  Fixture f;
  REQUIRE(f.ctx->activation().apply(ids({"uuid-e"})));

  CHECK(f.ctx->activation().dependentsOf(ids({"uuid-a"})) ==
        ids({"uuid-c", "uuid-e", "uuid-b"}));
}

TEST_CASE("dependents_of is empty for a mod without dependents") {
  Fixture f;

  CHECK(f.ctx->activation()
            .dependentsOf(ids({"uuid-d", "uuid-missing"}))
            .isEmpty());
}

TEST_CASE("removing a duplicate copy keeps dependents") {
  Fixture f;
  Mods mods = baseMods();
  mods.insert("uuid-a-copy", makeMod("Copy A", "mod.a"));
  f.reload(mods, {"uuid-a", "uuid-a-copy", "uuid-b", "uuid-c"});

  CHECK(f.ctx->activation().dependentsOf(ids({"uuid-a-copy"})).isEmpty());
  REQUIRE(f.ctx->activation().apply({}, ids({"uuid-a-copy"})));
  CHECK(f.ctx->activeUuids() == ids({"uuid-a", "uuid-b", "uuid-c"}));
  CHECK(f.ctx->activation().dependentsOf(ids({"uuid-a"})) ==
        ids({"uuid-b", "uuid-c"}));
}

TEST_CASE("enabling a replacement copy keeps dependents") {
  Fixture f;
  Mods mods = baseMods();
  mods.insert("uuid-a-copy", makeMod("Copy A", "mod.a"));
  f.reload(mods, {"uuid-a", "uuid-b", "uuid-c"});

  CHECK(f.ctx->activation()
            .dependentsOf(ids({"uuid-a"}), ids({"uuid-a-copy"}))
            .isEmpty());
  REQUIRE(f.ctx->activation().apply(ids({"uuid-a-copy"}), ids({"uuid-a"})));
  CHECK(f.ctx->activeUuids() == ids({"uuid-a-copy", "uuid-b", "uuid-c"}));
}

TEST_CASE("sidebar entries are published before the summary") {
  // The summary handler re-applies the sidebar filter, so entries must be
  // fresh.
  Fixture f;
  DiagnosticsService& diagnostics = f.ctx->diagnosticsService();
  QSet<QString> inactive;
  std::vector<QSet<QString>> seenAtSummary;

  diagnostics.sidebarEntriesChanged.connect(
      [&inactive](const QList<view::SidebarEntry>& entries) {
        inactive.clear();
        for (const view::SidebarEntry& entry : entries) {
          if (entry.label == QStringLiteral("Inactive")) {
            inactive = entry.visibleUuids;
          }
        }
      });
  diagnostics.diagnosticsSummaryChanged.connect(
      [&inactive,
       &seenAtSummary](const QHash<QString, view::ModDiagnosticsView>&) {
        seenAtSummary.push_back(inactive);
      });

  diagnostics.rebuild(f.ctx->activeUuids());
  seenAtSummary.clear();
  REQUIRE(f.ctx->activation().apply(ids({"uuid-f"})));

  REQUIRE_FALSE(seenAtSummary.empty());
  CHECK_FALSE(seenAtSummary.back().contains(QStringLiteral("uuid-f")));
}
