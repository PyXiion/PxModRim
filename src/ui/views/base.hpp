#pragma once

#include <QString>
#include <QVBoxLayout>

#include "ui/context.hpp"

namespace px::modrim {

// Whole-window view contributed to the left icon rail. Core never includes this:
// views are discovered entirely within ui/. Subclasses declare `kRailMeta`.
class BaseViewPanel : public RailView {
    Q_OBJECT

public:
    BaseViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app);

protected:
    void setClearColor(const QString& color);

    QVBoxLayout* root_ = nullptr;
};

}  // namespace px::modrim
