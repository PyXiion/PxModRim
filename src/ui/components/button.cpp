#include "ui/components/button.h"

#include <QCursor>

namespace px::modrim {

AppButton::AppButton(const QString& text, QWidget* parent) : QPushButton(text, parent) {
    setObjectName(QStringLiteral("appBtn"));
    setCursor(Qt::PointingHandCursor);
}

}  // namespace px::modrim
