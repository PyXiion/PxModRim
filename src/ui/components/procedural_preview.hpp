#pragma once
#include <QImage>
#include <QString>
namespace px::modrim {
QString initials(const QString& title);
QImage generatePreview(const QString& title, int width, int height);
}  // namespace px::modrim
