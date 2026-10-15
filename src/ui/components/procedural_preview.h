#pragma once
#include <QImage>
#include <QString>
namespace px::ui {
QString initials(const QString& title);
QImage generate_preview(const QString& title, int width, int height);
}
