#pragma once

#include <QQuickWidget>

class QQmlEngine;
class QWidget;

namespace px::modrim {

class HeaderController;

// Hosts Header.qml, the main window's top bar. The controller is injected and exposed
// to QML as `headerController`; the caller owns it and must outlive this panel.
class HeaderPanel : public QQuickWidget {
public:
    HeaderPanel(HeaderController& controller, QQmlEngine* engine, QWidget* parent = nullptr);
};

}  // namespace px::modrim
