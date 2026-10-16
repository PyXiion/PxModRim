#pragma once

#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <QCoro/QCoroTask>

#include <catch2/catch_test_macros.hpp>

namespace test_support {

// Qt resolves database driver plugins through the application instance, so
// tests that open databases create one for their scope (one at a time, as
// test_version does).
class TestApplication {
 public:
  TestApplication() : m_app(m_argc, nullptr) {}

 private:
  int m_argc = 0;
  QCoreApplication m_app;
};

// Drives a coroutine whose awaited work finishes without suspending, which
// holds for every coroutine these tests start. The REQUIRE fails if it
// suspended instead.
template <class T>
QCoro::Task<void> awaitInto(QCoro::Task<T> task, std::optional<T>& out) {
  out.emplace(co_await std::move(task));
}

template <class T>
T runSync(QCoro::Task<T> task) {
  std::optional<T> out;
  [[maybe_unused]] auto pending = awaitInto(std::move(task), out);
  REQUIRE(out.has_value());
  return std::move(*out);
}

inline bool writeBytes(const QString& path, std::string_view bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const auto size = static_cast<qint64>(bytes.size());
  return file.write(bytes.data(), size) == size;
}

inline std::string readBytes(const QString& path) {
  QFile file(path);
  REQUIRE(file.open(QIODevice::ReadOnly));
  const QByteArray data = file.readAll();
  return std::string(data.constData(), static_cast<std::size_t>(data.size()));
}

}  // namespace test_support
