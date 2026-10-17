#include "ui/views/base.hpp"

#include <QColor>
#include <QPalette>

#include "ui/theme/palette.hpp"

namespace px::modrim {

BaseViewPanel::BaseViewPanel(CoreContext& ctx, QQmlEngine* engine,
                             QWidget* parent, AppContext& app)
    : RailView(ctx, engine, parent, app) {
  setAutoFillBackground(true);
  m_root = new QVBoxLayout(this);
  m_root->setContentsMargins(0, 0, 0, 0);
  m_root->setSpacing(0);
  setClearColor(paletteValue(u"ELEVATE_0"));
}

void BaseViewPanel::setClearColor(const QString& color) {
  QPalette palette = this->palette();
  palette.setColor(QPalette::Window, QColor(color));
  setPalette(palette);
}

}  // namespace px::modrim
