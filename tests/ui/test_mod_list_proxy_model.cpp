#include <catch2/catch_test_macros.hpp>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include "ui/models/mod_list_proxy_model.h"
using namespace px::modrim;
namespace {
struct ProxyFixture {
    ModListModel source;
    ModListProxyModel proxy{&source};
    ProxyFixture() {
        QHash<QString,px::modrim::ListedModPtr> mods;
        for (int i = 0; i < 5; ++i) {
            auto mod = std::make_shared<px::modrim::AboutXmlMod>();
            mod->name = QStringLiteral("Mod %1").arg(i);
            mod->package_id = px::modrim::CaseInsensitiveStr(QStringLiteral("mod.%1").arg(i));
            mods.insert(QStringLiteral("uuid-%1").arg(i),mod);
        }
        source.load_mods(mods,{"uuid-0","uuid-2"});
    }
    QList<int> source_rows() const {
        QList<int> rows;
        for (int row = 0; row < proxy.rowCount(); ++row) rows.append(proxy.mapToSource(proxy.index(row)).row());
        return rows;
    }
    void check_round_trip() const {
        for (int row = 0; row < proxy.rowCount(); ++row) {
            const auto source_index = proxy.mapToSource(proxy.index(row));
            REQUIRE(source_index.isValid());
            CHECK(proxy.mapFromSource(source_index).row() == row);
        }
    }
};
}
TEST_CASE("mod proxy sidebar filtering maps subset empty and cleared states") {
    ProxyFixture f;
    CHECK(f.proxy.rowCount() == 5);
    f.proxy.set_sidebar_filter(QSet<QString>{"uuid-0","uuid-2","uuid-4"});
    CHECK(f.source_rows() == QList<int>{0,1,4});
    f.check_round_trip();
    f.proxy.set_sidebar_filter(QSet<QString>{});
    CHECK(f.proxy.rowCount() == 0);
    f.proxy.set_sidebar_filter(std::nullopt);
    CHECK(f.proxy.rowCount() == 5);
}
TEST_CASE("mod proxy search normalizes case whitespace package IDs and Unicode") {
    ProxyFixture f;
    f.proxy.set_search_filter("Mod 1"); CHECK(f.source_rows() == QList<int>{2});
    f.proxy.set_search_filter("mod.3"); CHECK(f.source_rows() == QList<int>{3});
    f.proxy.set_search_filter("MOD 2"); CHECK(f.source_rows() == QList<int>{1});
    f.proxy.set_search_filter("zzzz"); CHECK(f.proxy.rowCount() == 0);
    f.proxy.set_search_filter(" \t \n "); CHECK(f.proxy.rowCount() == 5);
    auto unicode = std::make_shared<px::modrim::AboutXmlMod>();
    unicode->name = QStringLiteral("Große Mod");
    unicode->package_id = px::modrim::CaseInsensitiveStr(QStringLiteral("grosse.mod"));
    auto padded = std::make_shared<px::modrim::AboutXmlMod>();
    padded->name = QStringLiteral("  Padded Mod  ");
    padded->package_id = px::modrim::CaseInsensitiveStr(QStringLiteral("  padded.pkg  "));
    f.source.load_mods({{"u",unicode},{"p",padded}},{"u","p"});
    f.proxy.set_search_filter(QStringLiteral("  GROßE  ")); CHECK(f.source_rows() == QList<int>{0});
    f.proxy.set_search_filter("  padded.pkg  "); CHECK(f.source_rows() == QList<int>{1});
}
TEST_CASE("mod proxy combines search and sidebar filters and exposes transitions") {
    ProxyFixture f;
    QSignalSpy filtered(&f.proxy,&ModListProxyModel::is_filtered_changed);
    f.proxy.set_sidebar_filter(QSet<QString>{"uuid-1","uuid-2","uuid-3"});
    f.proxy.set_search_filter("Mod 2"); CHECK(f.source_rows() == QList<int>{1});
    CHECK(f.proxy.is_filtered()); CHECK(filtered.size() == 1);
    f.proxy.set_search_filter("Mod 0"); CHECK(f.proxy.rowCount() == 0);
    f.proxy.set_search_filter(""); CHECK(f.proxy.rowCount() == 3);
    CHECK(filtered.size() == 1);
    f.proxy.set_sidebar_filter(std::nullopt); CHECK_FALSE(f.proxy.is_filtered()); CHECK(filtered.size() == 2);
    f.proxy.set_search_filter("mod"); CHECK(filtered.size() == 3);
    f.proxy.set_search_filter(""); CHECK(filtered.size() == 4);
}
TEST_CASE("mod proxy preserves persistent UUIDs that survive filtering") {
    ProxyFixture f;
    QPersistentModelIndex kept(f.proxy.index(2));
    QPersistentModelIndex hidden(f.proxy.index(1));
    f.proxy.set_sidebar_filter(QSet<QString>{"uuid-1","uuid-3"});
    CHECK(kept.isValid()); CHECK(kept.row() == 0);
    CHECK(kept.data(ModListModel::UuidRole).toString() == "uuid-1");
    CHECK_FALSE(hidden.isValid());
    CHECK_FALSE(f.proxy.mapFromSource(f.source.index(0)).isValid());
    f.proxy.set_sidebar_filter(std::nullopt);
    f.proxy.set_search_filter("Mod 4"); CHECK(f.source_rows() == QList<int>{4});
    f.proxy.set_search_filter(""); f.check_round_trip();
}
TEST_CASE("mod proxy moves retain source mappings with and without filters") {
    ProxyFixture f;
    REQUIRE(f.proxy.move_row(0,2)); f.check_round_trip();
    ProxyFixture filtered;
    filtered.proxy.set_sidebar_filter(QSet<QString>{"uuid-0","uuid-1","uuid-4"});
    REQUIRE(filtered.proxy.move_row(0,1)); filtered.check_round_trip();
    CHECK_FALSE(filtered.proxy.mapFromSource(filtered.source.index(0)).isValid());
}
TEST_CASE("mod proxy clamped move stays inside the unfiltered active block") {
    ProxyFixture f;
    REQUIRE(f.proxy.active_count() == 2);
    REQUIRE(f.proxy.move_row(0,4,true));
    CHECK(f.proxy.data(f.proxy.index(0),ModListModel::UuidRole).toString() == "uuid-2");
    CHECK(f.proxy.data(f.proxy.index(1),ModListModel::UuidRole).toString() == "uuid-0");
    CHECK(f.proxy.data(f.proxy.index(2),ModListModel::UuidRole).toString() == "uuid-1");
    CHECK_FALSE(f.proxy.move_row(2,0,true));
    f.proxy.set_search_filter("mod"); CHECK_FALSE(f.proxy.move_row(0,1,true));
    f.check_round_trip();
}
TEST_CASE("mod proxy emits data changes only after move completion") {
    ProxyFixture f;
    f.source.load_mods({{"u0",std::make_shared<px::modrim::ListedMod>()},
                        {"u1",std::make_shared<px::modrim::ListedMod>()},
                        {"u2",std::make_shared<px::modrim::ListedMod>()}},{"u0","u1","u2"});
    QStringList events;
    QObject::connect(&f.proxy,&QAbstractItemModel::rowsAboutToBeMoved,&f.proxy,[&] { events.append("begin"); });
    QObject::connect(&f.proxy,&QAbstractItemModel::rowsMoved,&f.proxy,[&] { events.append("end"); });
    QObject::connect(&f.proxy,&QAbstractItemModel::dataChanged,&f.proxy,[&](const QModelIndex& top,const QModelIndex& bottom) {
        events.append(QStringLiteral("data:%1:%2").arg(top.row()).arg(bottom.row()));
    });
    REQUIRE(f.proxy.move_row(0,2));
    CHECK(events == QStringList{"begin","end","data:0:2"});
    for (int row = 0; row < 3; ++row) CHECK(f.proxy.data(f.proxy.index(row),ModListModel::LoadIndexRole).toInt() == row+1);
}
TEST_CASE("mod proxy section counts reflect visible rows rather than the whole collection") {
    ProxyFixture f;
    f.proxy.set_sidebar_filter(QSet<QString>{"uuid-0","uuid-1"});
    CHECK(f.proxy.active_count() == 1);
    CHECK(f.proxy.data(f.proxy.index(1),ModListModel::SectionNameRole).toString() == "Inactive (1)");
    f.proxy.set_sidebar_filter(QSet<QString>{"uuid-1"});
    CHECK(f.proxy.data(f.proxy.index(0),ModListModel::SectionNameRole).toString().isEmpty());
}
