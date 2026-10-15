#include <catch2/catch_session.hpp>

#include <QApplication>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(PxModRimPlugin)

// One QApplication for the whole UI test binary: Qt allows a single instance per process.
int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
