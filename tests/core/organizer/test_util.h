#pragma once

#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <QCoro/QCoroTask>
#include <QString>

#include "core/error.h"
#include "../test_support.h"

namespace organizer_test {

// Runs a coroutine to completion on the Qt event loop. Needs a test_support::TestApplication in scope.
template <class T>
T wait(QCoro::Task<T> task) {
    return QCoro::waitFor(std::move(task));
}

template <class T>
T expect(px::core::Result<T> result) {
    REQUIRE(result.has_value());
    return std::move(*result);
}
inline void expect(const px::core::Result<void>& result) {
    REQUIRE(result.has_value());
}


template <class T>
void expect_error(const px::core::Result<T>& result, const QString& fragment = {}) {
    REQUIRE_FALSE(result.has_value());
    if (!fragment.isEmpty()) {
        CHECK(result.error().message.contains(fragment));
    }
}

}  // namespace organizer_test
