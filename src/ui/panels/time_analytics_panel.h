#pragma once
#include <optional>
#include <QCoro/QCoroTask>
#include <QDialog>
#include <QStringList>
#include <QWidget>
#include "core/services/startup_impact_service/service.h"
class QQmlEngine;
class QQuickWidget;
namespace px::modrim {
class TimeAnalyticsPanel : public QWidget {
    Q_OBJECT
public:
    TimeAnalyticsPanel(startup_impact::StartupImpactService* service, QQmlEngine* engine, QWidget* parent);
    QCoro::Task<void> setData(std::optional<QString> pid, QStringList activePids);
    void clear();
private:
    startup_impact::StartupImpactService* service_;
    QQuickWidget* qml_;
    quint64 requestToken_ = 0;
};
class StartupImpactDialog : public QDialog {
    Q_OBJECT
public:
    StartupImpactDialog(startup_impact::StartupImpactService* service, QQmlEngine* engine, QWidget* parent);
    TimeAnalyticsPanel* panel;
};
} // namespace px::modrim
