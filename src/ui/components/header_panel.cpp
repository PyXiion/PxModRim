#include "ui/components/header_panel.h"

#include <QQmlContext>
#include <QQuickItem>
#include <QUrl>
#include <spdlog/spdlog.h>

#include "ui/components/header_controller.h"
#include "ui/theme/palette.h"

namespace px::modrim {

HeaderPanel::HeaderPanel(HeaderController& controller, QQmlEngine* engine, QWidget* parent)
    : QQuickWidget(engine, parent) {
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(paletteColor(u"ELEVATE_1"));
    rootContext()->setContextProperty(QStringLiteral("headerController"), &controller);
    setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/Header.qml")));
    if (rootObject() == nullptr) {
        spdlog::error("Header.qml failed to load: rootObject is null");
    }
}

}  // namespace px::modrim
