#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QLayout>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <catch2/catch_test_macros.hpp>

#include "core/loading.hpp"
#include "ui/components/progress_dialog.hpp"
#include "ui/components/toast.hpp"

namespace {

using px::modrim::ProgressDialog;
using px::modrim::Toast;
using px::modrim::ToastLevel;
using px::modrim::ToastManager;

QApplication& application() {
  if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *app;
  }
  static int argc = 1;
  static char name[] = "test_toast_and_progress";
  static char* argv[] = {name, nullptr};
  static QApplication app(argc, argv);
  return app;
}

// Runs the event loop for `ms` so animations and timers can fire.
void waitMs(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

}  // namespace

TEST_CASE("close button removes a persistent toast") {
  application();
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
  application();
  QWidget parent;
  ToastManager manager(&parent);
  manager.info(QStringLiteral("gone soon"), 50);
  REQUIRE(manager.layout()->count() == 1);

  waitMs(600);

  REQUIRE(manager.layout()->count() == 0);
}

TEST_CASE(
    "progress dialog stays visible when a new task starts before it hides") {
  application();
  QWidget parent;
  px::modrim::LoadingState loading(&parent);
  ProgressDialog dialog(loading, &parent);

  {
    auto first = loading.task(QStringLiteral("first"));
  }
  {
    auto second = loading.task(QStringLiteral("second"));
    waitMs(500);
    REQUIRE(dialog.isVisible());
    REQUIRE(dialog.windowTitle() == QStringLiteral("second"));
  }

  waitMs(800);
  REQUIRE_FALSE(dialog.isVisible());
}
