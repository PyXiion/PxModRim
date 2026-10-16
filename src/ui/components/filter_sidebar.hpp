#pragma once
#include <QQuickWidget>
class QAbstractItemModel;
namespace px::modrim {
class FilterSidebar : public QQuickWidget {
    Q_OBJECT
public:
    FilterSidebar(QQmlEngine* engine, QAbstractItemModel* model, QWidget* parent = nullptr);
    int currentIndex() const;
signals:
    void activated(int index);
};
}
