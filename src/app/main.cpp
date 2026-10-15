#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

#include "core/version.h"

Q_IMPORT_QML_PLUGIN(PxModRimPlugin)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PxModRim"));
    app.setApplicationVersion(px::core::appVersion());

    QQmlApplicationEngine engine;
    engine.loadFromModule("PxModRim", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
