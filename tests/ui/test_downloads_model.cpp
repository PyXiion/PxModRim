#include <catch2/catch_test_macros.hpp>

#include <QHash>

#include "ui/plugins/downloads/model.h"

namespace {
QHash<QString, QString> states(const px::ui::DownloadsModel& model) {
    QHash<QString, QString> result;
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row);
        result.insert(model.data(index, px::ui::DownloadsModel::IdRole).toString(),
                      model.data(index, px::ui::DownloadsModel::StateRole).toString());
    }
    return result;
}
}

TEST_CASE("download item events resolve states and cancel leftover rows") {
    px::ui::DownloadsModel model;
    model.begin({"1", "2", "3", "4", "5"}, {{"1", "One"}});
    model.apply({"1", "downloading", 0, 0});
    model.apply({"2", "downloading", 5, 10});
    model.apply({"3", "downloading", 10, 10});
    model.apply({"4", "error", 0, 0, "boom"});
    model.flush();
    const QHash<QString, QString> expected{{"1", "unchanged"}, {"2", "downloading"}, {"3", "updated"}, {"4", "failed"}, {"5", "queued"}};
    CHECK(states(model) == expected);
    model.finish({{"1", "3"}, {"4"}, {"3"}});
    CHECK(states(model).value("2") == "cancelled");
    CHECK(states(model).value("5") == "cancelled");
    CHECK(model.updated() == 1);
    CHECK(model.unchanged() == 1);
    CHECK(model.failed() == 3);
    CHECK(model.active() == 0);
    CHECK(model.failed_ids() == QStringList{"2", "4", "5"});
}

TEST_CASE("download filters limit visible rows and keep fallback titles") {
    px::ui::DownloadsModel model;
    model.begin({"1", "2"}, {{"1", "One"}});
    model.apply({"2", "error", 0, 0, "x"});
    model.flush();
    model.setFilter(QStringLiteral("failed"));
    REQUIRE(model.rowCount() == 1);
    CHECK(model.data(model.index(0), px::ui::DownloadsModel::TitleRole).toString() == "2");
    model.setFilter(QStringLiteral("all"));
    CHECK(model.rowCount() == 2);
}
