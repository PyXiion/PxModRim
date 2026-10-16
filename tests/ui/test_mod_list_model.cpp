#include <catch2/catch_test_macros.hpp>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include "ui/models/mod_list_model.hpp"
#include "ui/models/impact.hpp"
#include "ui/theme/palette.hpp"
using namespace px::modrim;
namespace {
struct ListFixture {
    QHash<QString, px::modrim::ListedModPtr> mods;
    ModListModel model{{{QStringLiteral("stub"),QStringLiteral("#abcdef")}}};
    ListFixture() {
        for (int i = 0; i < 5; ++i) {
            auto mod = std::make_shared<px::modrim::AboutXmlMod>();
            mod->name = QStringLiteral("Mod %1").arg(i);
            mod->package_id = px::modrim::CaseInsensitiveStr(QStringLiteral("mod.%1").arg(i));
            mod->provider_id = QStringLiteral("stub");
            mods.insert(QStringLiteral("uuid-%1").arg(i),mod);
        }
        model.load_mods(mods,{"uuid-0","uuid-2"});
    }
    QStringList order() const {
        QStringList result;
        for (int row = 0; row < model.rowCount(); ++row) result.append(model.get_item(row)->uuid);
        return result;
    }
    QList<int> indices() const {
        QList<int> result;
        for (int row = 0; row < model.rowCount(); ++row) result.append(model.data(model.index(row),ModListModel::LoadIndexRole).toInt());
        return result;
    }
};
}
TEST_CASE("mod list load skips missing active UUIDs and sorts inactive mods") {
    ListFixture f;
    CHECK(f.order() == QStringList{"uuid-0","uuid-2","uuid-1","uuid-3","uuid-4"});
    f.model.load_mods(f.mods,{"ghost","uuid-2"});
    CHECK(f.model.active_uuids() == QStringList{"uuid-2"});
    CHECK(f.model.rowCount() == 5);
    CHECK(f.model.rowCount(f.model.index(0)) == 0);
    CHECK_FALSE(f.model.data({}).isValid());
    CHECK_FALSE(f.model.setData({},Qt::Checked,ModListModel::CheckStateRole));
}
TEST_CASE("mod list check state emits role updates without resetting") {
    ListFixture f;
    QSignalSpy reset(&f.model,&QAbstractItemModel::modelReset);
    QSignalSpy changed(&f.model,&QAbstractItemModel::dataChanged);
    QSignalSpy active(&f.model,&ModListModel::active_mods_changed);
    REQUIRE(f.model.setData(f.model.index(2),Qt::Checked,ModListModel::CheckStateRole));
    CHECK(f.model.get_item(2)->checked);
    CHECK(f.indices() == QList<int>{1,2,3,-1,-1});
    CHECK(reset.isEmpty());
    REQUIRE(changed.size() == 1);
    CHECK(changed.first()[2].value<QList<int>>().contains(ModListModel::LoadIndexRole));
    CHECK(active.size() == 1);
    changed.clear();
    REQUIRE(f.model.setData(f.model.index(2),Qt::Checked,ModListModel::CheckStateRole));
    CHECK(changed.isEmpty());
    f.model.set_check_states({0},false);
    CHECK(f.indices() == QList<int>{-1,1,2,-1,-1});
}
TEST_CASE("mod list commits subset order preserving persistent UUID identities") {
    ListFixture f;
    QPersistentModelIndex persistent(f.model.index(0));
    QSignalSpy about(&f.model,&QAbstractItemModel::layoutAboutToBeChanged);
    QSignalSpy changed(&f.model,&QAbstractItemModel::layoutChanged);
    f.model.commitOrder({"uuid-2","uuid-0"});
    CHECK(f.order() == QStringList{"uuid-2","uuid-0","uuid-1","uuid-3","uuid-4"});
    CHECK(persistent.row() == 1);
    CHECK(persistent.data(ModListModel::UuidRole).toString() == "uuid-0");
    CHECK(about.size() == 1);
    CHECK(changed.size() == 1);
    f.model.commitOrder(f.order());
    CHECK(changed.size() == 1);
    f.model.commitOrder({"uuid-4","uuid-3","uuid-1","uuid-0","uuid-2"});
    CHECK(f.order() == QStringList{"uuid-4","uuid-3","uuid-1","uuid-0","uuid-2"});
    CHECK(f.model.active_uuids() == QStringList{"uuid-0","uuid-2"});
}
TEST_CASE("mod list move updates load indices and rejects invalid moves") {
    ListFixture f;
    QSignalSpy changed(&f.model,&QAbstractItemModel::dataChanged);
    CHECK(f.indices() == QList<int>{1,2,-1,-1,-1});
    REQUIRE(f.model.move_row(0,1));
    CHECK(f.order().first() == "uuid-2");
    CHECK(f.indices() == QList<int>{1,2,-1,-1,-1});
    REQUIRE_FALSE(changed.isEmpty());
    CHECK(changed.first()[2].value<QList<int>>().contains(ModListModel::LoadIndexRole));
    CHECK_FALSE(f.model.move_row(0,0));
    CHECK_FALSE(f.model.move_row(-1,1));
    CHECK_FALSE(f.model.move_row(0,5));
}
TEST_CASE("mod list provider labels and QML roles match the reference") {
    CHECK(provider_label("steam") == "Workshop");
    CHECK(provider_label("downloaded") == "Workshop");
    CHECK(provider_label("local") == "Local");
    CHECK(provider_label("core","ludeon.rimworld") == "Core");
    CHECK(provider_label("core","ludeon.rimworld.royalty") == "DLC");
    CHECK(provider_label("unknown","ludeon.rimworld.ideology") == "DLC");
    ListFixture f;
    const auto roles = f.model.roleNames();
    CHECK(roles.value(ModListModel::CheckStateRole) == "checkState");
    CHECK(roles.value(ModListModel::SectionNameRole) == "sectionName");
    CHECK(roles.value(ModListModel::StartupImpactTextRole) == "startupImpactText");
    CHECK(f.model.data(f.model.index(0),ModListModel::ProviderColorRole).toString() == "#abcdef");
    CHECK(f.model.data(f.model.index(2),ModListModel::SectionNameRole).toString() == "Inactive (3)");
}
TEST_CASE("mod list startup impacts include presentation roles and empty input is silent") {
    ModListModel empty;
    QSignalSpy no_changes(&empty,&QAbstractItemModel::dataChanged);
    empty.set_startup_impact({{"mod.0",1.5}});
    CHECK(no_changes.isEmpty());
    ListFixture f;
    f.model.set_startup_impact({{"mod.0",1.5}});
    CHECK(f.model.data(f.model.index(0),ModListModel::StartupImpactRole).toDouble() == 1.5);
    CHECK(f.model.data(f.model.index(0),ModListModel::StartupImpactTextRole).toString() == "1.50 s");
    CHECK(f.model.data(f.model.index(1),ModListModel::StartupImpactRole).toDouble() == 0);
    CHECK(format_duration(0.0001) == "0 ms");
    CHECK(format_duration(0.5) == "500 ms");
    CHECK(format_duration(60) == "1m 00s");
    CHECK(impact_color(0.5) == paletteValue(u"SUCCESS"));
    CHECK(impact_color(1) == paletteValue(u"WARNING"));
    CHECK(impact_color(5) == paletteValue(u"DANGER"));
}
