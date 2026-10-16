#include <QPersistentModelIndex>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

#include "ui/models/mod_list_proxy_model.hpp"
using namespace px::modrim;
namespace {
struct ProxyFixture {
  ModListModel source;
  ModListProxyModel proxy{&source};
  ProxyFixture() {
    QHash<QString, px::modrim::ListedModPtr> mods;
    for (int i = 0; i < 5; ++i) {
      auto mod = std::make_shared<px::modrim::AboutXmlMod>();
      mod->name = QStringLiteral("Mod %1").arg(i);
      mod->packageId =
          px::modrim::CaseInsensitiveStr(QStringLiteral("mod.%1").arg(i));
      mods.insert(QStringLiteral("uuid-%1").arg(i), mod);
    }
    source.loadMods(mods, {"uuid-0", "uuid-2"});
  }
  QList<int> sourceRows() const {
    QList<int> rows;
    for (int row = 0; row < proxy.rowCount(); ++row)
      rows.append(proxy.mapToSource(proxy.index(row)).row());
    return rows;
  }
  void checkRoundTrip() const {
    for (int row = 0; row < proxy.rowCount(); ++row) {
      const auto sourceIndex = proxy.mapToSource(proxy.index(row));
      REQUIRE(sourceIndex.isValid());
      CHECK(proxy.mapFromSource(sourceIndex).row() == row);
    }
  }
};
}  // namespace
TEST_CASE("mod proxy sidebar filtering maps subset empty and cleared states") {
  ProxyFixture f;
  CHECK(f.proxy.rowCount() == 5);
  f.proxy.setSidebarFilter(QSet<QString>{"uuid-0", "uuid-2", "uuid-4"});
  CHECK(f.sourceRows() == QList<int>{0, 1, 4});
  f.checkRoundTrip();
  f.proxy.setSidebarFilter(QSet<QString>{});
  CHECK(f.proxy.rowCount() == 0);
  f.proxy.setSidebarFilter(std::nullopt);
  CHECK(f.proxy.rowCount() == 5);
}
TEST_CASE(
    "mod proxy search normalizes case whitespace package IDs and Unicode") {
  ProxyFixture f;
  f.proxy.setSearchFilter("Mod 1");
  CHECK(f.sourceRows() == QList<int>{2});
  f.proxy.setSearchFilter("mod.3");
  CHECK(f.sourceRows() == QList<int>{3});
  f.proxy.setSearchFilter("MOD 2");
  CHECK(f.sourceRows() == QList<int>{1});
  f.proxy.setSearchFilter("zzzz");
  CHECK(f.proxy.rowCount() == 0);
  f.proxy.setSearchFilter(" \t \n ");
  CHECK(f.proxy.rowCount() == 5);
  auto unicode = std::make_shared<px::modrim::AboutXmlMod>();
  unicode->name = QStringLiteral("Große Mod");
  unicode->packageId =
      px::modrim::CaseInsensitiveStr(QStringLiteral("grosse.mod"));
  auto padded = std::make_shared<px::modrim::AboutXmlMod>();
  padded->name = QStringLiteral("  Padded Mod  ");
  padded->packageId =
      px::modrim::CaseInsensitiveStr(QStringLiteral("  padded.pkg  "));
  f.source.loadMods({{"u", unicode}, {"p", padded}}, {"u", "p"});
  f.proxy.setSearchFilter(QStringLiteral("  GROßE  "));
  CHECK(f.sourceRows() == QList<int>{0});
  f.proxy.setSearchFilter("  padded.pkg  ");
  CHECK(f.sourceRows() == QList<int>{1});
}
TEST_CASE(
    "mod proxy combines search and sidebar filters and exposes transitions") {
  ProxyFixture f;
  QSignalSpy filtered(&f.proxy, &ModListProxyModel::isFilteredChanged);
  f.proxy.setSidebarFilter(QSet<QString>{"uuid-1", "uuid-2", "uuid-3"});
  f.proxy.setSearchFilter("Mod 2");
  CHECK(f.sourceRows() == QList<int>{1});
  CHECK(f.proxy.isFiltered());
  CHECK(filtered.size() == 1);
  f.proxy.setSearchFilter("Mod 0");
  CHECK(f.proxy.rowCount() == 0);
  f.proxy.setSearchFilter("");
  CHECK(f.proxy.rowCount() == 3);
  CHECK(filtered.size() == 1);
  f.proxy.setSidebarFilter(std::nullopt);
  CHECK_FALSE(f.proxy.isFiltered());
  CHECK(filtered.size() == 2);
  f.proxy.setSearchFilter("mod");
  CHECK(filtered.size() == 3);
  f.proxy.setSearchFilter("");
  CHECK(filtered.size() == 4);
}
TEST_CASE("mod proxy preserves persistent UUIDs that survive filtering") {
  ProxyFixture f;
  QPersistentModelIndex kept(f.proxy.index(2));
  QPersistentModelIndex hidden(f.proxy.index(1));
  f.proxy.setSidebarFilter(QSet<QString>{"uuid-1", "uuid-3"});
  CHECK(kept.isValid());
  CHECK(kept.row() == 0);
  CHECK(kept.data(ModListModel::kUuidRole).toString() == "uuid-1");
  CHECK_FALSE(hidden.isValid());
  CHECK_FALSE(f.proxy.mapFromSource(f.source.index(0)).isValid());
  f.proxy.setSidebarFilter(std::nullopt);
  f.proxy.setSearchFilter("Mod 4");
  CHECK(f.sourceRows() == QList<int>{4});
  f.proxy.setSearchFilter("");
  f.checkRoundTrip();
}
TEST_CASE("mod proxy moves retain source mappings with and without filters") {
  ProxyFixture f;
  REQUIRE(f.proxy.moveRow(0, 2));
  f.checkRoundTrip();
  ProxyFixture filtered;
  filtered.proxy.setSidebarFilter(QSet<QString>{"uuid-0", "uuid-1", "uuid-4"});
  REQUIRE(filtered.proxy.moveRow(0, 1));
  filtered.checkRoundTrip();
  CHECK_FALSE(filtered.proxy.mapFromSource(filtered.source.index(0)).isValid());
}
TEST_CASE("mod proxy clamped move stays inside the unfiltered active block") {
  ProxyFixture f;
  REQUIRE(f.proxy.activeCount() == 2);
  REQUIRE(f.proxy.moveRow(0, 4, true));
  CHECK(f.proxy.data(f.proxy.index(0), ModListModel::kUuidRole).toString() ==
        "uuid-2");
  CHECK(f.proxy.data(f.proxy.index(1), ModListModel::kUuidRole).toString() ==
        "uuid-0");
  CHECK(f.proxy.data(f.proxy.index(2), ModListModel::kUuidRole).toString() ==
        "uuid-1");
  CHECK_FALSE(f.proxy.moveRow(2, 0, true));
  f.proxy.setSearchFilter("mod");
  CHECK_FALSE(f.proxy.moveRow(0, 1, true));
  f.checkRoundTrip();
}
TEST_CASE("mod proxy emits data changes only after move completion") {
  ProxyFixture f;
  f.source.loadMods({{"u0", std::make_shared<px::modrim::ListedMod>()},
                     {"u1", std::make_shared<px::modrim::ListedMod>()},
                     {"u2", std::make_shared<px::modrim::ListedMod>()}},
                    {"u0", "u1", "u2"});
  QStringList events;
  QObject::connect(&f.proxy, &QAbstractItemModel::rowsAboutToBeMoved, &f.proxy,
                   [&] { events.append("begin"); });
  QObject::connect(&f.proxy, &QAbstractItemModel::rowsMoved, &f.proxy,
                   [&] { events.append("end"); });
  QObject::connect(
      &f.proxy, &QAbstractItemModel::dataChanged, &f.proxy,
      [&](const QModelIndex& top, const QModelIndex& bottom) {
        events.append(
            QStringLiteral("data:%1:%2").arg(top.row()).arg(bottom.row()));
      });
  REQUIRE(f.proxy.moveRow(0, 2));
  CHECK(events == QStringList{"begin", "end", "data:0:2"});
  for (int row = 0; row < 3; ++row)
    CHECK(f.proxy.data(f.proxy.index(row), ModListModel::kLoadIndexRole)
              .toInt() == row + 1);
}
TEST_CASE(
    "mod proxy section counts reflect visible rows rather than the whole "
    "collection") {
  ProxyFixture f;
  f.proxy.setSidebarFilter(QSet<QString>{"uuid-0", "uuid-1"});
  CHECK(f.proxy.activeCount() == 1);
  CHECK(f.proxy.data(f.proxy.index(1), ModListModel::kSectionNameRole)
            .toString() == "Inactive (1)");
  f.proxy.setSidebarFilter(QSet<QString>{"uuid-1"});
  CHECK(f.proxy.data(f.proxy.index(0), ModListModel::kSectionNameRole)
            .toString()
            .isEmpty());
}
