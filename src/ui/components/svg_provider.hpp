#pragma once

#include <QPixmap>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

class QObject;
class QQmlEngine;

namespace px::modrim {

// Serves named SVG icons to QML via image://icons/<name>?color=<hex>.
// An unknown name renders an empty SVG, so the image is transparent.
class SvgIconProvider : public QQuickImageProvider {
 public:
  SvgIconProvider();

  QPixmap requestPixmap(const QString& id, QSize* size,
                        const QSize& requestedSize) override;
};

// The one way to build a QML engine: every QML surface shares it, and it always
// carries the `icons` provider, so image://icons/… URLs resolve.
QQmlEngine* createQmlEngine(QObject* parent);

}  // namespace px::modrim
