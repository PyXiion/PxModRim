#include "ui/components/filter_sidebar.h"
#include "ui/theme/palette.h"
#include <QAbstractItemModel>
#include <QQmlContext>
#include <QQuickItem>
namespace px::ui {
FilterSidebar::FilterSidebar(QQmlEngine* engine, QAbstractItemModel* model, QWidget* parent)
    : QQuickWidget(engine, parent) {
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(paletteColor(u"ELEVATE_2"));
    rootContext()->setContextProperty(QStringLiteral("filterSidebarModel"), model);
    setInitialProperties({{QStringLiteral("model"), QVariant::fromValue(model)}});
    setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/FilterSidebar.qml")));
    if (auto* root = rootObject()) connect(root, SIGNAL(activated(int)), this, SIGNAL(activated(int)));
}
int FilterSidebar::current_index() const {
    const auto* root = rootObject();
    const auto* view = root ? root->findChild<QObject*>(QStringLiteral("listView")) : nullptr;
    return view ? view->property("currentIndex").toInt() : -1;
}
}
