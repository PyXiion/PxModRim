#include <catch2/catch_test_macros.hpp>

#include "ui/window/restart.hpp"

using namespace px::modrim;

TEST_CASE("Restart preserves the executable and original arguments") {
  const QStringList arguments{"pxmodrim",        "--ui-scale", "175",
                              "-platform",       "offscreen",  "--unknown",
                              "path with spaces"};
  const auto command = restartCommand("/opt/PxModRim/pxmodrim", arguments,
                                      QProcessEnvironment{}, false);

  CHECK(command.program == "/opt/PxModRim/pxmodrim");
  CHECK(command.arguments == arguments.sliced(1));
  CHECK(restartCommand("pxmodrim", {"pxmodrim"}, QProcessEnvironment{}, false)
            .arguments.isEmpty());
}

TEST_CASE("Restart removes only the application-generated Qt scale") {
  QProcessEnvironment environment;
  environment.insert("QT_SCALE_FACTOR", "1.75");
  environment.insert("PX_UI_SCALE", "150");
  environment.insert("QT_QPA_PLATFORM", "offscreen");
  environment.insert("PATH", "/usr/bin");

  const auto command =
      restartCommand("pxmodrim", {"pxmodrim"}, environment, false);
  auto expected = environment;
  expected.remove("QT_SCALE_FACTOR");

  CHECK(command.environment == expected);
  CHECK(environment.value("QT_SCALE_FACTOR") == "1.75");
}

TEST_CASE("Restart retains a user-provided Qt scale unchanged") {
  QProcessEnvironment environment;
  environment.insert("QT_SCALE_FACTOR", "1.5");
  environment.insert("PX_UI_SCALE", "175");

  CHECK(
      restartCommand("pxmodrim", {"pxmodrim"}, environment, true).environment ==
      environment);

  environment.insert("QT_SCALE_FACTOR", "");
  CHECK(
      restartCommand("pxmodrim", {"pxmodrim"}, environment, true).environment ==
      environment);
}
