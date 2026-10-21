#include <catch2/catch_test_macros.hpp>

#include "ui/window/geometry.hpp"

using namespace px::modrim;

TEST_CASE("Initial window geometry stays within its available screen") {
  const QRect desktop(0, 0, 1920, 1080);
  const QRect initial(260, 115, 1400, 850);
  CHECK(fitWindowGeometry(initial, desktop) == initial);

  const QRect small(0, 0, 1280, 720);
  const QRect shrunk = fitWindowGeometry(initial, small);
  CHECK(shrunk.size() == QSize(1240, 680));
  CHECK(small.contains(shrunk));

  const QRect floorScreen(0, 0, 920, 580);
  const QRect floor = fitWindowGeometry(initial, floorScreen);
  CHECK(floor.size() == QSize(900, 560));
  CHECK(floorScreen.contains(floor));

  const QRect tinyScreen(0, 0, 800, 480);
  const QRect tiny = fitWindowGeometry(initial, tinyScreen);
  CHECK(tiny == tinyScreen);

  const QRect tooSmall(100, 100, 400, 300);
  CHECK(fitWindowGeometry(tooSmall, desktop).size() == QSize(900, 560));

  const QRect otherScreen(-1920, 100, 1920, 1080);
  const QRect offscreen(4000, -2000, 1400, 850);
  const QRect moved = fitWindowGeometry(offscreen, otherScreen);
  CHECK(moved.size() == offscreen.size());
  CHECK(otherScreen.contains(moved));
  CHECK(moved.right() == otherScreen.right() - 20);
  CHECK(moved.top() == otherScreen.top() + 20);
  const QRect opposite =
      fitWindowGeometry(QRect(-5000, 5000, 1400, 850), otherScreen);
  CHECK(opposite.left() == otherScreen.left() + 20);
  CHECK(opposite.bottom() == otherScreen.bottom() - 20);
}
