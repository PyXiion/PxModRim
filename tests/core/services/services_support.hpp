#pragma once

#include <optional>
#include <type_traits>
#include <variant>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QCoro/QCoroTask>
#include <QFile>
#include <QIODevice>
#include <QString>

namespace services_test {

// Owns a QCoreApplication for one test case, unless one already exists (test_providers keeps a
// process-wide instance, and Qt asserts when a second one is created).
class EventLoop {
public:
    EventLoop() {
        if (!QCoreApplication::instance()) {
            m_app.emplace(m_argc, nullptr);
        }
    }

private:
    int m_argc = 0;
    std::optional<QCoreApplication> m_app;
};

// Runs a coroutine to completion on the event loop. Coroutines that never suspend finish
// before the loop starts.
template <class T>
T runAsync(QCoro::Task<T> task) {
    // std::optional<void> is ill-formed, so a void result is carried as std::monostate.
    using Outcome = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    std::optional<Outcome> result;
    auto driver = [](QCoro::Task<T> inner, std::optional<Outcome>* out) -> QCoro::Task<void> {
        if constexpr (std::is_void_v<T>) {
            co_await std::move(inner);
            out->emplace();
        } else {
            *out = co_await std::move(inner);
        }
        QCoreApplication::quit();
    };
    driver(std::move(task), &result);
    if (!result) {
        QCoreApplication::exec();
    }
    REQUIRE(result.has_value());
    if constexpr (std::is_void_v<T>) {
        return;
    } else {
        return std::move(*result);
    }
}

inline bool writeText(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

}  // namespace services_test
