#pragma once

#include <QMenuBar>

#include "ui/window/actions.h"

namespace px::ui {

struct MenuSpec {
    QString title;
    std::vector<std::optional<ActionId>> actions;
};

const std::array<MenuSpec, 4>& menuLayout();

class MenuBar : public QMenuBar {
    Q_OBJECT
public:
    explicit MenuBar(const ActionMap& actions, QWidget* parent = nullptr);
};

}  // namespace px::ui
