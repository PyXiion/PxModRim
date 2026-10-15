#pragma once

#include <QPushButton>
#include <QString>

class QWidget;

namespace px::ui {

// Square icon-only button (QSS `iconBtn`). `primary` paints it in the accent color.
class IconButton : public QPushButton {
public:
    IconButton(const QString& iconName, const QString& tooltip = {}, bool primary = false,
               int size = 32, QWidget* parent = nullptr);

    void setIconColor(const QString& color);

private:
    QString icon_name_;
    int icon_px_;
};

}  // namespace px::ui
