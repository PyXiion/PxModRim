#include <QEventLoop>
#include <QLayout>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <catch2/catch_test_macros.hpp>

#include "ui/components/toast.hpp"

namespace {

using px::modrim::Toast;
using px::modrim::ToastLevel;
using px::modrim::ToastManager;

// Runs the event loop for `ms` so animations and timers can fire.
void waitMs(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

}  // namespace

TEST_CASE("close button removes a persistent toast") {
  QWidget parent;
  ToastManager manager(&parent);
  manager.showToast(QStringLiteral("stays"), ToastLevel::kInfo, 0);

  auto* toast = manager.findChild<Toast*>();
  REQUIRE(toast != nullptr);
  auto* close = toast->findChild<QPushButton*>(QStringLiteral("toastClose"));
  REQUIRE(close != nullptr);

  close->click();
  waitMs(300);

  REQUIRE(manager.layout()->count() == 0);
}

TEST_CASE("timed toast dismisses itself after its duration") {
  QWidget parent;
  ToastManager manager(&parent);
  manager.info(QStringLiteral("gone soon"), 50);
  REQUIRE(manager.layout()->count() == 1);

  waitMs(600);

  REQUIRE(manager.layout()->count() == 0);
}
