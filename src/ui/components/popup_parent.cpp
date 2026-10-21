#include "ui/components/popup_parent.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace px::modrim {

namespace {

bool isPopupWindow(const QObject* object) {
  const auto* window = qobject_cast<const QQuickWindow*>(object);
  return window != nullptr && window->transientParent() != nullptr &&
         (window->type() == Qt::Popup || window->type() == Qt::ToolTip) &&
         qstrcmp(window->metaObject()->className(), "QQuickPopupWindow") == 0;
}

const QObject* windowPopup(const QQuickWindow* popupWindow) {
  for (const QQuickItem* child : popupWindow->contentItem()->childItems()) {
    const QObject* popup = child->parent();
    if (popup != nullptr && popup->property("parent").value<QQuickItem*>()) {
      return popup;
    }
  }
  return nullptr;
}

// Qt Quick hands the compositor the popup's anchor rectangle in its own scene
// units, relative to the QQuickWidget. The compositor reads it as a rectangle
// in the top-level surface's native pixels, so it is off by the widget's
// position and, under QT_SCALE_FACTOR, by the scale. Same geometry as
// QQuickPopupWindow::parentControlGeometry, corrected, and handed over through
// the QtWayland popup anchor property.
void anchorToSurface(QQuickWindow* popupWindow) {
  const QObject* popup = windowPopup(popupWindow);
  if (popup == nullptr) {
    return;
  }
  const auto* parentItem = popup->property("parent").value<QQuickItem*>();
  if (parentItem->window() == nullptr) {
    return;
  }
  bool scaleValid = false;
  double scale = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&scaleValid);
  if (!scaleValid || scale <= 0) {
    scale = 1;
  }

  const QWindow* surfaceParent = popupWindow->transientParent();
  while (surfaceParent->handle() == nullptr &&
         surfaceParent->transientParent() != nullptr) {
    surfaceParent = surfaceParent->transientParent();
  }

  const qreal overlap = popup->property("overlap").toReal();
  const QRectF bounds = parentItem->boundingRect();
  const QPointF topLeft =
      parentItem->mapToScene(bounds.topLeft()) +
      QPointF(parentItem->window()->position() - surfaceParent->position());
  const qreal width =
      std::max(std::abs(bounds.width() - overlap * 2), bounds.width() / 4);
  const QRect anchor(qFloor((topLeft.x() + overlap) * scale),
                     qFloor(topLeft.y() * scale), qCeil(width * scale),
                     qCeil(bounds.height() * scale));
  // An empty anchor is a protocol error; without one QtWayland falls back to
  // the window geometry, which is already in surface pixels.
  if (anchor.isValid()) {
    popupWindow->setProperty("_q_waylandPopupAnchorRect", anchor);
  }
}

}  // namespace

QuickPopupParenting::QuickPopupParenting(QObject* parent) : QObject(parent) {}

bool QuickPopupParenting::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::Show) {
    if (auto* quick = qobject_cast<QQuickWidget*>(watched)) {
      if (QWindow* top = quick->window()->windowHandle()) {
        quick->quickWindow()->setTransientParent(top);
      }
    }
  } else if (event->type() == QEvent::PlatformSurface &&
             static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
                 QPlatformSurfaceEvent::SurfaceCreated &&
             QGuiApplication::platformName().startsWith(
                 QLatin1String("wayland")) &&
             isPopupWindow(watched)) {
    anchorToSurface(static_cast<QQuickWindow*>(watched));
  }
  return false;
}

}  // namespace px::modrim
