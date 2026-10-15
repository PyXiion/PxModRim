#pragma once
#include <QQuickWidget>
class QAbstractItemModel;
namespace px::ui {
class FilterSidebar : public QQuickWidget {
    Q_OBJECT
public:
    FilterSidebar(QQmlEngine* engine, QAbstractItemModel* model, QWidget* parent = nullptr);
    int current_index() const;
signals:
    void activated(int index);
};
}
