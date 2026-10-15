#include "ui/views/base.h"

#include <QColor>
#include <QPalette>

#include "ui/theme/palette.h"

namespace px::ui {

BaseViewPanel::BaseViewPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : RailView(ctx, engine, parent, app) {
    setAutoFillBackground(true);
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(0, 0, 0, 0);
    root_->setSpacing(0);
    setClearColor(paletteValue(u"ELEVATE_0"));
}

void BaseViewPanel::setClearColor(const QString& color) {
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, QColor(color));
    setPalette(palette);
}

}  // namespace px::ui
