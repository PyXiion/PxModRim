#include <QHash>
#include <QSet>

#include <catch2/catch_test_macros.hpp>

#include "ui/window/restart.hpp"

using namespace px::modrim;

namespace {

const QString kAppImage = "/home/user/Apps/PxModRim.AppImage";
const QString kAppDir = "/tmp/.mount_PxModR";
const QString kMountedExecutable = "/tmp/.mount_PxModR/usr/bin/pxmodrim";

// A fake filesystem: the files that exist, and canonical paths of every
// existing path (missing paths canonicalise to an empty string).
struct FakeFiles {
  QSet<QString> files{kAppImage, kMountedExecutable,
                      "/tmp/.mount_PxModR-other/usr/bin/pxmodrim",
                      "/usr/bin/pxmodrim"};
  QHash<QString, QString> canonical{
      {kAppImage, kAppImage},
      {kAppDir, kAppDir},
      {"/tmp/.mount_PxModR/", kAppDir},
      {kMountedExecutable, kMountedExecutable},
      {"/tmp/link/pxmodrim", kMountedExecutable},
      {"/tmp/.mount_PxModR-other/usr/bin/pxmodrim",
       "/tmp/.mount_PxModR-other/usr/bin/pxmodrim"},
      {"/usr/bin/pxmodrim", "/usr/bin/pxmodrim"}};

  QString appImageFor(const QProcessEnvironment& environment,
                      const QString& executable) const {
    return relaunchAppImage(
        environment, executable,
        [this](const QString& path) { return files.contains(path); },
        [this](const QString& path) { return canonical.value(path); });
  }
};

QProcessEnvironment appImageEnvironment(const QString& appImage = kAppImage,
                                        const QString& appDir = kAppDir) {
  QProcessEnvironment environment;
  environment.insert("APPIMAGE", appImage);
  environment.insert("APPDIR", appDir);
  return environment;
}

}  // namespace

TEST_CASE("Restart preserves the executable and original arguments") {
  const QStringList arguments{"pxmodrim",        "--ui-scale", "175",
                              "-platform",       "offscreen",  "--unknown",
                              "path with spaces"};
  const auto command = restartCommand("/opt/PxModRim/pxmodrim", {}, arguments,
                                      QProcessEnvironment{}, false);

  CHECK(command.program == "/opt/PxModRim/pxmodrim");
  CHECK(command.arguments == arguments.sliced(1));
  CHECK(
      restartCommand("pxmodrim", {}, {"pxmodrim"}, QProcessEnvironment{}, false)
          .arguments.isEmpty());
}

TEST_CASE("Restart relaunches the AppImage that runs this executable") {
  const FakeFiles files;
  const auto environment = appImageEnvironment();
  const QStringList arguments{kMountedExecutable, "--ui-scale", "175",
                              "path with spaces"};

  const QString appImage = files.appImageFor(environment, kMountedExecutable);
  CHECK(appImage == kAppImage);
  const auto command = restartCommand(kMountedExecutable, appImage, arguments,
                                      environment, false);
  CHECK(command.program == kAppImage);
  CHECK(command.arguments == arguments.sliced(1));

  CHECK(files.appImageFor(environment, "/tmp/link/pxmodrim") == kAppImage);
  CHECK(files.appImageFor(appImageEnvironment(kAppImage, kAppDir + "/"),
                          kMountedExecutable) == kAppImage);
}

TEST_CASE("Restart ignores an AppImage that does not run this executable") {
  const FakeFiles files;

  // Inherited from a parent AppImage while running from a normal install.
  CHECK(
      files.appImageFor(appImageEnvironment(), "/usr/bin/pxmodrim").isEmpty());
  CHECK(files
            .appImageFor(appImageEnvironment(),
                         "/tmp/.mount_PxModR-other/usr/bin/pxmodrim")
            .isEmpty());
  CHECK(files.appImageFor(appImageEnvironment(), kAppDir).isEmpty());
  CHECK(
      files
          .appImageFor(appImageEnvironment("/home/user/Apps/Missing.AppImage"),
                       kMountedExecutable)
          .isEmpty());
  CHECK(
      files.appImageFor(appImageEnvironment(""), kMountedExecutable).isEmpty());
  CHECK(
      files.appImageFor(appImageEnvironment(kAppImage, ""), kMountedExecutable)
          .isEmpty());
  CHECK(files
            .appImageFor(appImageEnvironment(kAppImage, "/tmp/missing"),
                         kMountedExecutable)
            .isEmpty());
  CHECK(files.appImageFor(QProcessEnvironment{}, kMountedExecutable).isEmpty());

  const QStringList arguments{"/usr/bin/pxmodrim", "--unknown"};
  const auto command = restartCommand("/usr/bin/pxmodrim", {}, arguments,
                                      appImageEnvironment(), false);
  CHECK(command.program == "/usr/bin/pxmodrim");
  CHECK(command.arguments == arguments.sliced(1));
}

TEST_CASE("Restart removes only the application-generated Qt scale") {
  QProcessEnvironment environment;
  environment.insert("QT_SCALE_FACTOR", "1.75");
  environment.insert("PX_UI_SCALE", "150");
  environment.insert("QT_QPA_PLATFORM", "offscreen");
  environment.insert("PATH", "/usr/bin");

  const auto command =
      restartCommand("pxmodrim", {}, {"pxmodrim"}, environment, false);
  auto expected = environment;
  expected.remove("QT_SCALE_FACTOR");

  CHECK(command.environment == expected);
  CHECK(environment.value("QT_SCALE_FACTOR") == "1.75");
}

TEST_CASE("Restart retains a user-provided Qt scale unchanged") {
  QProcessEnvironment environment;
  environment.insert("QT_SCALE_FACTOR", "1.5");
  environment.insert("PX_UI_SCALE", "175");

  CHECK(restartCommand("pxmodrim", {}, {"pxmodrim"}, environment, true)
            .environment == environment);

  environment.insert("QT_SCALE_FACTOR", "");
  CHECK(restartCommand("pxmodrim", {}, {"pxmodrim"}, environment, true)
            .environment == environment);
}
