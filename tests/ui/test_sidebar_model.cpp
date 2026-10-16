#include <catch2/catch_test_macros.hpp>
#include <QSignalSpy>
#include "ui/models/sidebar_model.hpp"
#include "ui/theme/palette.hpp"
TEST_CASE("sidebar entry updates refresh provider icons without resetting") {
    using px::modrim::view::SidebarEntry;
    px::modrim::SidebarModel model;
    model.setEntries({{SidebarEntry::Kind::kProvider,"Steam",{"a","b"},"steam"}});
    QSignalSpy changed(&model,&QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model,&QAbstractItemModel::modelReset);
    model.updateEntries({{SidebarEntry::Kind::kProvider,"Local",{"a","b"},"local"}});
    CHECK(model.data(model.index(0),px::modrim::SidebarModel::kIconRole).toString() == "folder");
    CHECK(model.data(model.index(0),px::modrim::SidebarModel::kIconColorRole).toString() == px::modrim::paletteValue(u"TEXT_DIM"));
    CHECK(model.data(model.index(0),px::modrim::SidebarModel::kCountRole).toInt() == 2);
    CHECK(reset.isEmpty());
    REQUIRE(changed.size() == 1);
    CHECK(changed.first()[2].value<QList<int>>().contains(px::modrim::SidebarModel::kIconRole));
    CHECK(model.getEntry(0)->providerId == "local");
}
TEST_CASE("sidebar models status sections and resets when its entry shape changes") {
    using px::modrim::view::SidebarEntry;
    using px::modrim::SidebarModel;
    SidebarModel model;
    model.setEntries({{SidebarEntry::Kind::kAll,"All",{"a"},{}},
                       {SidebarEntry::Kind::kWithErrors,"Errors",{"a"},{}}});
    CHECK(model.data(model.index(0),SidebarModel::kSectionRole).toString() == "Status");
    CHECK(model.data(model.index(1),SidebarModel::kBadgeFgRole).toString() == px::modrim::paletteValue(u"DANGER"));
    CHECK_FALSE(model.getEntry(-1).has_value());
    QSignalSpy reset(&model,&QAbstractItemModel::modelReset);
    model.updateEntries({});
    CHECK(reset.size() == 1);
    CHECK(model.rowCount() == 0);
}
