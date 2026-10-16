#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QString>

#include <catch2/catch_test_macros.hpp>

#include "ui/components/procedural_preview.hpp"

namespace {

// generate_preview paints with QPainter and QFont, so it needs a GUI
// application. Reuse one if the test binary already has it; otherwise create
// one, offscreen unless the environment picks a platform.
QApplication& application() {
  if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
    return *app;
  }
  static int argc = 1;
  static char name[] = "pxmodrim_ui_tests";
  static char* argv[] = {name, nullptr};
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  static QApplication app(argc, argv);
  return app;
}

}  // namespace

TEST_CASE("initials abbreviates titles") {
  struct Case {
    const char* title;
    const char* expected;
  };
  const Case cases[] = {
      {"[AV] Framework - Russian Translation", "FR"},
      {"SF Grim Reality", "SG"},
      {"Vanilla Expanded Framework", "VE"},
      {"RimworldTweaks", "RT"},
      {"Combat", "CO"},
      {"[AV]", "AV"},
      {"[AV] Framework", "AF"},
      {"The Lord of Rings", "LR"},
      {"Русский перевод", "РП"},
      {"---", "--"},
  };
  for (const Case& c : cases) {
    INFO("title: " << c.title);
    CHECK(px::modrim::initials(QString::fromUtf8(c.title)) ==
          QString::fromUtf8(c.expected));
  }
}

TEST_CASE("generate preview returns a non-null image of the requested size") {
  application();
  const QImage image =
      px::modrim::generatePreview(QStringLiteral("[AV] Framework"), 120, 60);
  REQUIRE(!image.isNull());
  CHECK(image.width() == 120);
  CHECK(image.height() == 60);
}

TEST_CASE("generate preview returns the cached image for the same request") {
  application();
  const QImage image =
      px::modrim::generatePreview(QStringLiteral("[AV] Framework"), 120, 60);
  REQUIRE(!image.isNull());
  // QImage shares its data implicitly, so a cache hit is identified by the
  // shared data's cacheKey.
  const QImage again =
      px::modrim::generatePreview(QStringLiteral("[AV] Framework"), 120, 60);
  CHECK(again.cacheKey() == image.cacheKey());
}
