#include <catch2/catch_test_macros.hpp>
#include <QSignalSpy>
#include "ui/models/sidebar_model.h"
#include "ui/theme/palette.h"
TEST_CASE("sidebar entry updates refresh provider icons without resetting") {
    using px::core::view::SidebarEntry;
    px::ui::SidebarModel model;
    model.set_entries({{SidebarEntry::Kind::Provider,"Steam",{"a","b"},"steam"}});
    QSignalSpy changed(&model,&QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model,&QAbstractItemModel::modelReset);
    model.update_entries({{SidebarEntry::Kind::Provider,"Local",{"a","b"},"local"}});
    CHECK(model.data(model.index(0),px::ui::SidebarModel::IconRole).toString() == "folder");
    CHECK(model.data(model.index(0),px::ui::SidebarModel::IconColorRole).toString() == px::ui::paletteValue(u"TEXT_DIM"));
    CHECK(model.data(model.index(0),px::ui::SidebarModel::CountRole).toInt() == 2);
    CHECK(reset.isEmpty());
    REQUIRE(changed.size() == 1);
    CHECK(changed.first()[2].value<QList<int>>().contains(px::ui::SidebarModel::IconRole));
    CHECK(model.get_entry(0)->provider_id == "local");
}
TEST_CASE("sidebar models status sections and resets when its entry shape changes") {
    using px::core::view::SidebarEntry;
    using px::ui::SidebarModel;
    SidebarModel model;
    model.set_entries({{SidebarEntry::Kind::All,"All",{"a"},{}},
                       {SidebarEntry::Kind::WithErrors,"Errors",{"a"},{}}});
    CHECK(model.data(model.index(0),SidebarModel::SectionRole).toString() == "Status");
    CHECK(model.data(model.index(1),SidebarModel::BadgeFgRole).toString() == px::ui::paletteValue(u"DANGER"));
    CHECK_FALSE(model.get_entry(-1).has_value());
    QSignalSpy reset(&model,&QAbstractItemModel::modelReset);
    model.update_entries({});
    CHECK(reset.size() == 1);
    CHECK(model.rowCount() == 0);
}
