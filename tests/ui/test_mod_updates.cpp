#include <catch2/catch_test_macros.hpp>
#include <QAbstractButton>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>
#include "ui/components/mod_updates.h"
#include "../core/services/services_support.h"

TEST_CASE("small updates skip confirmation") {
    QWidget parent;
    CHECK(services_test::run_async(px::ui::confirm_update(px::ui::CONFIRM_THRESHOLD-1,&parent)));
    CHECK(parent.findChildren<QMessageBox*>().isEmpty());
}
TEST_CASE("large updates follow the actual confirmation dialog answer") {
    QWidget parent;
    for (const auto answer : {QMessageBox::Yes,QMessageBox::Cancel}) {
        QTimer click(&parent);
        click.setInterval(1);
        QObject::connect(&click,&QTimer::timeout,&parent,[&] {
            for (auto* box : parent.findChildren<QMessageBox*>()) if (box->isVisible()) {
                click.stop(); box->button(answer)->click(); return;
            }
        });
        click.start();
        CHECK(services_test::run_async(px::ui::confirm_update(px::ui::CONFIRM_THRESHOLD,&parent)) == (answer == QMessageBox::Yes));
    }
}
