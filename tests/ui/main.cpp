#include <QApplication>

#include <QtQml/QQmlExtensionPlugin>

#include <catch2/catch_session.hpp>

Q_IMPORT_QML_PLUGIN(PxModRimPlugin)

// One QApplication for the whole UI test binary: Qt allows a single instance
// per process.
int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QApplication app(argc, argv);
  return Catch::Session().run(argc, argv);
}
