#include "ui/components/view_rail_panel.h"

#include <QQmlContext>
#include <QQuickItem>
#include <QUrl>
#include <spdlog/spdlog.h>

#include "ui/theme/constants.h"
#include "ui/theme/palette.h"

namespace px::modrim {

ViewRailPanel::ViewRailPanel(const QVariantList& tabs, const QVariantList& helpEntries, QQmlEngine* engine,
                             QWidget* parent)
    : QQuickWidget(engine, parent) {
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(paletteColor(u"ELEVATE_2"));
    setMinimumWidth(RAIL_MIN_WIDTH);
    setMaximumWidth(RAIL_MAX_WIDTH);

    rootContext()->setContextProperty(QStringLiteral("railModel"), tabs);
    rootContext()->setContextProperty(QStringLiteral("railHelpEntries"), helpEntries);
    setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/IconRail.qml")));

    QObject* root = rootObject();
    if (root == nullptr) {
        spdlog::error("IconRail.qml failed to load: rootObject is null");
        return;
    }
    connect(root, SIGNAL(tabSelected(int)), this, SIGNAL(currentChanged(int)));
    connect(root, SIGNAL(tabHovered(int)), this, SIGNAL(hovered(int)));
    connect(root, SIGNAL(settingsRequested()), this, SIGNAL(settingsRequested()));
    connect(root, SIGNAL(helpActionRequested(QString)), this, SIGNAL(helpActionRequested(QString)));
}

void ViewRailPanel::setCurrent(int index) {
    if (QObject* root = rootObject()) {
        root->setProperty("currentIndex", index);
    }
}

}  // namespace px::modrim
