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

  QString programFor(const QProcessEnvironment& environment,
                     const QString& executable) const {
    return restartProgram(
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

TEST_CASE("Restart preserves the program and original arguments") {
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

TEST_CASE("Restart relaunches the AppImage that runs this executable") {
  const FakeFiles files;

  CHECK(files.programFor(appImageEnvironment(), kMountedExecutable) ==
        kAppImage);
  CHECK(files.programFor(appImageEnvironment(), "/tmp/link/pxmodrim") ==
        kAppImage);
  CHECK(files.programFor(appImageEnvironment(kAppImage, kAppDir + "/"),
                         kMountedExecutable) == kAppImage);
}

TEST_CASE("Restart ignores an AppImage that does not run this executable") {
  const FakeFiles files;
  const auto keepsExecutable = [&](const QProcessEnvironment& environment,
                                   const QString& executable) {
    return files.programFor(environment, executable) == executable;
  };

  // Inherited from a parent AppImage while running from a normal install.
  CHECK(keepsExecutable(appImageEnvironment(), "/usr/bin/pxmodrim"));
  CHECK(keepsExecutable(appImageEnvironment(),
                        "/tmp/.mount_PxModR-other/usr/bin/pxmodrim"));
  CHECK(keepsExecutable(appImageEnvironment(), kAppDir));
  CHECK(keepsExecutable(appImageEnvironment("/home/user/Apps/Missing.AppImage"),
                        kMountedExecutable));
  CHECK(keepsExecutable(appImageEnvironment(""), kMountedExecutable));
  CHECK(
      keepsExecutable(appImageEnvironment(kAppImage, ""), kMountedExecutable));
  CHECK(keepsExecutable(appImageEnvironment(kAppImage, "/tmp/missing"),
                        kMountedExecutable));
  CHECK(keepsExecutable(QProcessEnvironment{}, kMountedExecutable));
}

TEST_CASE("Restart drops only an application-generated Qt scale") {
  QProcessEnvironment environment;
  environment.insert("QT_SCALE_FACTOR", "1.75");
  environment.insert("PX_UI_SCALE", "150");
  environment.insert("PATH", "/usr/bin");

  auto expected = environment;
  expected.remove("QT_SCALE_FACTOR");
  CHECK(restartCommand("pxmodrim", {"pxmodrim"}, environment, false)
            .environment == expected);

  CHECK(
      restartCommand("pxmodrim", {"pxmodrim"}, environment, true).environment ==
      environment);
  environment.insert("QT_SCALE_FACTOR", "");
  CHECK(
      restartCommand("pxmodrim", {"pxmodrim"}, environment, true).environment ==
      environment);
}
