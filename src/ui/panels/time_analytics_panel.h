#pragma once
#include <optional>
#include <QCoro/QCoroTask>
#include <QDialog>
#include <QStringList>
#include <QWidget>
#include "core/services/startup_impact_service/service.h"
class QQmlEngine;
class QQuickWidget;
namespace px::ui {
class TimeAnalyticsPanel : public QWidget {
    Q_OBJECT
public:
    TimeAnalyticsPanel(core::startup_impact::StartupImpactService* service, QQmlEngine* engine, QWidget* parent);
    QCoro::Task<void> setData(std::optional<QString> pid, QStringList activePids);
    void clear();
private:
    core::startup_impact::StartupImpactService* service_;
    QQuickWidget* qml_;
    quint64 requestToken_ = 0;
};
class StartupImpactDialog : public QDialog {
    Q_OBJECT
public:
    StartupImpactDialog(core::startup_impact::StartupImpactService* service, QQmlEngine* engine, QWidget* parent);
    TimeAnalyticsPanel* panel;
};
} // namespace px::ui
