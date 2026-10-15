#include "ui/components/popup_parent.h"

#include <QEvent>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QWidget>
#include <QWindow>

namespace px::ui {

QuickPopupParenting::QuickPopupParenting(QObject* parent) : QObject(parent) {}

bool QuickPopupParenting::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show) {
        if (auto* quick = qobject_cast<QQuickWidget*>(watched)) {
            if (QWindow* top = quick->window()->windowHandle()) {
                quick->quickWindow()->setTransientParent(top);
            }
        }
    }
    return false;
}

}  // namespace px::ui
