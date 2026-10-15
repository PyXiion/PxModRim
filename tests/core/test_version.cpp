#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QCoro/QCoroTask>

#include "core/version.h"

TEST_CASE("coroutine resumes on the Qt event loop") {
    int argc = 0;
    QCoreApplication app(argc, nullptr);

    QString result;
    auto run = [&]() -> QCoro::Task<> {
        result = co_await px::core::delayedVersion(5);
        app.quit();
    };
    run();
    app.exec();

    REQUIRE(result == px::core::appVersion());
}
