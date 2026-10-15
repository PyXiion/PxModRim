#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>

#include "core/config.h"
#include "core/context.h"
#include "core/models/metadata/structures.h"
#include "core/models/view/sidebar.h"
#include "core/services/activation_service.h"
#include "core/services/diagnostics_service.h"
#include "services_support.h"

using namespace px::core;

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
ListedModPtr make_mod(const char* name,
                      const char* pid,
                      std::initializer_list<const char*> deps = {},
                      std::initializer_list<const char*> load_after = {},
                      std::initializer_list<const char*> load_before = {}) {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = QString::fromLatin1(name);
    mod->package_id = CaseInsensitiveStr(QString::fromLatin1(pid));
    mod->provider_id = QStringLiteral("stub");
    mod->valid = true;
    mod->about_rules.load_after = CaseInsensitiveSet(ids(load_after));
    mod->about_rules.load_before = CaseInsensitiveSet(ids(load_before));
    for (const char* dep : deps) {
        DependencyMod dependency;
        dependency.name = QString::fromLatin1(dep);
        dependency.package_id = CaseInsensitiveStr(QString::fromLatin1(dep));
        mod->about_rules.set_dependency(dependency);
    }
    return mod;
}

Mods base_mods() {
    Mods mods;
    mods.insert("uuid-a", make_mod("Mod A", "mod.a"));
    mods.insert("uuid-b", make_mod("Mod B", "mod.b", {"mod.a"}));
    mods.insert("uuid-c", make_mod("Mod C", "mod.c", {"mod.b"}));
    mods.insert("uuid-d", make_mod("Mod D", "mod.d"));
    mods.insert("uuid-e", make_mod("Mod E", "mod.e", {"mod.a"}));
    mods.insert("uuid-f", make_mod("Mod F", "mod.f"));
    mods.insert("uuid-g", make_mod("Mod G", "mod.g", {"mod.f"}));
    mods.insert("uuid-h", make_mod("Mod H", "mod.h", {}, {"mod.b"}, {"mod.a"}));
    mods.insert("uuid-i", make_mod("Mod I", "mod.i", {}, {"mod.d"}));
    mods.insert("uuid-j", make_mod("Mod J", "mod.j", {}, {}, {"mod.a"}));
    return mods;
}

// Owns the config directory and the context for one test. Starts with the base mods loaded as
// c, d, a, b and the diagnostics rebuilt.
struct Fixture {
    services_test::EventLoop loop;
    QTemporaryDir dir;
    ConfigService config_service{dir.path()};
    std::unique_ptr<CoreContext> ctx = CoreContext::create(AppConfig{}, &config_service);

    Fixture() {
        ctx->load(base_mods(), ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b"}));
        ctx->diagnosticsService().rebuild();
    }

    void reload(Mods mods, std::initializer_list<const char*> active) {
        ctx->load(std::move(mods), ids(active));
        ctx->diagnosticsService().rebuild();
    }
};

}  // namespace

TEST_CASE("enabling inserts after dependency without reordering existing mods") {
    Fixture f;
    std::vector<QStringList> emitted;
    f.ctx->activeStateChanged().connect([&emitted](const QStringList& uuids) {
        emitted.push_back(uuids);
    });

    REQUIRE(f.ctx->activation().apply(ids({"uuid-f", "uuid-e"})));

    const QStringList expected = ids({"uuid-c", "uuid-d", "uuid-a", "uuid-e", "uuid-b", "uuid-f"});
    CHECK(f.ctx->activeUuids() == expected);
    REQUIRE(emitted.size() == 1);
    CHECK(emitted.front() == expected);
}

TEST_CASE("unconstrained enable appends") {
    Fixture f;

    REQUIRE(f.ctx->activation().apply(ids({"uuid-f"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"}));
}

TEST_CASE("enabling mod with load-after rule inserts after target") {
    Fixture f;

    REQUIRE(f.ctx->activation().apply(ids({"uuid-i"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-i", "uuid-a", "uuid-b"}));
}

TEST_CASE("enabling mod with load-before rule inserts before target") {
    Fixture f;

    REQUIRE(f.ctx->activation().apply(ids({"uuid-j"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-j", "uuid-a", "uuid-b"}));
}

TEST_CASE("enabling dependency before active dependent") {
    Fixture f;
    f.reload(base_mods(), {"uuid-b", "uuid-c", "uuid-d"});

    REQUIRE(f.ctx->activation().apply(ids({"uuid-a"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-a", "uuid-b", "uuid-c", "uuid-d"}));
}

TEST_CASE("conflicting constraints fall back to appending") {
    Fixture f;

    REQUIRE(f.ctx->activation().apply(ids({"uuid-h"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-h"}));
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

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"}));
}

TEST_CASE("disabling removes and keeps remaining order") {
    Fixture f;

    REQUIRE(f.ctx->activation().apply({}, ids({"uuid-d", "uuid-a"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-b"}));
}

TEST_CASE("unknown ids are ignored") {
    Fixture f;

    REQUIRE(f.ctx->activation().apply(ids({"uuid-missing", "uuid-f"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"}));
}

TEST_CASE("mixed selection applies in a single emission") {
    Fixture f;
    std::vector<QStringList> emitted;
    f.ctx->activeStateChanged().connect([&emitted](const QStringList& uuids) {
        emitted.push_back(uuids);
    });

    REQUIRE(f.ctx->activation().apply(ids({"uuid-f", "uuid-e"}), ids({"uuid-a", "uuid-d"})));

    const QStringList expected = ids({"uuid-c", "uuid-b", "uuid-f", "uuid-e"});
    CHECK(f.ctx->activeUuids() == expected);
    REQUIRE(emitted.size() == 1);
    CHECK(emitted.front() == expected);
}

TEST_CASE("uuid in both enable and disable favors disable") {
    Fixture f;
    std::vector<QStringList> emitted;
    f.ctx->activeStateChanged().connect([&emitted](const QStringList& uuids) {
        emitted.push_back(uuids);
    });

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
        f.ctx->activeStateChanged().connect([&emissions](const QStringList&) { ++emissions; });

        CHECK_FALSE(f.ctx->activation().apply(enable, disable));

        CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-a", "uuid-b"}));
        CHECK(emissions == 0);
    }
}

TEST_CASE("edges through an uninstalled mod add no constraint") {
    Fixture f;
    Mods mods;
    mods.insert("uuid-a", make_mod("Mod A", "mod.a"));
    mods.insert("uuid-w", make_mod("Mod W", "mod.w", {}, {"ce.absent"}));
    mods.insert("uuid-y", make_mod("Mod Y", "mod.y", {}, {}, {"ce.absent"}));
    f.reload(mods, {"uuid-a", "uuid-w"});

    REQUIRE(f.ctx->activation().apply(ids({"uuid-y"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-a", "uuid-w", "uuid-y"}));
}

TEST_CASE("bounds ignore an inconsistent active order") {
    Fixture f;
    Mods mods = base_mods();
    // Active c depends on b but sits before it; X must still land in d..b.
    mods.insert("uuid-x", make_mod("Mod X", "mod.x", {"mod.d"}, {}, {"mod.b"}));
    f.reload(mods, {"uuid-c", "uuid-d", "uuid-a", "uuid-b"});

    REQUIRE(f.ctx->activation().apply(ids({"uuid-x"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-x", "uuid-a", "uuid-b"}));
}

TEST_CASE("batch cycle does not unplace downstream mods") {
    Fixture f;
    Mods mods = base_mods();
    mods.insert("uuid-p", make_mod("Mod P", "mod.p", {"mod.q"}));
    mods.insert("uuid-q", make_mod("Mod Q", "mod.q", {"mod.p"}));
    mods.insert("uuid-z", make_mod("Mod Z", "mod.z", {"mod.p"}));
    mods.insert("uuid-k", make_mod("Mod K", "mod.k", {"mod.d"}, {}, {"mod.a"}));
    f.reload(mods, {"uuid-c", "uuid-d", "uuid-a", "uuid-b"});

    REQUIRE(f.ctx->activation().apply(ids({"uuid-z", "uuid-k", "uuid-q", "uuid-p"})));

    CHECK(f.ctx->activeUuids() == ids({"uuid-c", "uuid-d", "uuid-k", "uuid-a", "uuid-b",
                                       "uuid-q", "uuid-p", "uuid-z"}));
}

TEST_CASE("dependents_of lists transitive dependents in active order") {
    Fixture f;

    CHECK(f.ctx->activation().dependents_of(ids({"uuid-a"})) == ids({"uuid-c", "uuid-b"}));
}

TEST_CASE("dependents_of excludes its inputs") {
    Fixture f;

    CHECK(f.ctx->activation().dependents_of(ids({"uuid-a", "uuid-b"})) == ids({"uuid-c"}));
}

TEST_CASE("dependents_of excludes inactive dependents") {
    Fixture f;

    CHECK_FALSE(f.ctx->activeUuids().contains(QStringLiteral("uuid-e")));
    CHECK_FALSE(f.ctx->activation().dependents_of(ids({"uuid-a"})).contains(QStringLiteral("uuid-e")));
}

TEST_CASE("dependents_of reflects the graph after a state change") {
    Fixture f;
    REQUIRE(f.ctx->activation().apply(ids({"uuid-e"})));

    CHECK(f.ctx->activation().dependents_of(ids({"uuid-a"})) == ids({"uuid-c", "uuid-e", "uuid-b"}));
}

TEST_CASE("dependents_of is empty for a mod without dependents") {
    Fixture f;

    CHECK(f.ctx->activation().dependents_of(ids({"uuid-d", "uuid-missing"})).isEmpty());
}

TEST_CASE("removing a duplicate copy keeps dependents") {
    Fixture f;
    Mods mods = base_mods();
    mods.insert("uuid-a-copy", make_mod("Copy A", "mod.a"));
    f.reload(mods, {"uuid-a", "uuid-a-copy", "uuid-b", "uuid-c"});

    CHECK(f.ctx->activation().dependents_of(ids({"uuid-a-copy"})).isEmpty());
    REQUIRE(f.ctx->activation().apply({}, ids({"uuid-a-copy"})));
    CHECK(f.ctx->activeUuids() == ids({"uuid-a", "uuid-b", "uuid-c"}));
    CHECK(f.ctx->activation().dependents_of(ids({"uuid-a"})) == ids({"uuid-b", "uuid-c"}));
}

TEST_CASE("enabling a replacement copy keeps dependents") {
    Fixture f;
    Mods mods = base_mods();
    mods.insert("uuid-a-copy", make_mod("Copy A", "mod.a"));
    f.reload(mods, {"uuid-a", "uuid-b", "uuid-c"});

    CHECK(f.ctx->activation().dependents_of(ids({"uuid-a"}), ids({"uuid-a-copy"})).isEmpty());
    REQUIRE(f.ctx->activation().apply(ids({"uuid-a-copy"}), ids({"uuid-a"})));
    CHECK(f.ctx->activeUuids() == ids({"uuid-a-copy", "uuid-b", "uuid-c"}));
}

TEST_CASE("sidebar entries are published before the summary") {
    // The summary handler re-applies the sidebar filter, so entries must be fresh.
    Fixture f;
    DiagnosticsService& diagnostics = f.ctx->diagnosticsService();
    QSet<QString> inactive;
    std::vector<QSet<QString>> seen_at_summary;

    diagnostics.sidebar_entries_changed.connect([&inactive](const QList<view::SidebarEntry>& entries) {
        inactive.clear();
        for (const view::SidebarEntry& entry : entries) {
            if (entry.label == QStringLiteral("Inactive")) {
                inactive = entry.visible_uuids;
            }
        }
    });
    diagnostics.diagnostics_summary_changed.connect(
        [&inactive, &seen_at_summary](const QHash<QString, view::ModDiagnosticsView>&) {
            seen_at_summary.push_back(inactive);
        });

    diagnostics.rebuild(f.ctx->activeUuids());
    seen_at_summary.clear();
    REQUIRE(f.ctx->activation().apply(ids({"uuid-f"})));

    REQUIRE_FALSE(seen_at_summary.empty());
    CHECK_FALSE(seen_at_summary.back().contains(QStringLiteral("uuid-f")));
}
