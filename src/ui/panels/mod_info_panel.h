#pragma once
#include <QPointer>
#include <QWidget>
#include <QVariant>
#include "ui/mod_selection.h"
#include "ui/ui_prefs.h"
class QQmlEngine;
class QQuickWidget;
class QLabel;
namespace px::modrim {
class ModInfoBanner;
class StartupImpactDialog;
class ModInfoPanel : public QWidget, public ModInfoSink {
    Q_OBJECT
public:
    ModInfoPanel(CoreContext& ctx, QQmlEngine* engine, UiPrefs& prefs, QWidget* parent = nullptr);
    ~ModInfoPanel() override;
    void showMod(const ListedMod& mod, const QList<view::ModIssueView>& issues) override;
    void setIssues(const QList<view::ModIssueView>& issues);
    void clear() override;
    QCoro::Task<void> setTimeAnalytics(const std::optional<QString>& pid, const QStringList& activePids) override;
signals:
    void modRequested(const QString& uuid);
private slots:
    void openFolder();
    void openUrl();
    void copyText(const QString& text);
    void descToggled(bool expanded);
    void updateMod();
    void openStartupDetails();
private:
    void setQml(const char* name, const QVariant& value);
    void syncUpdating();
    QCoro::Task<void> loadPreview(std::optional<QString> path, QString title, quint64 token);
    QCoro::Task<void> performUpdate(QString id);
    QCoro::Task<void> showStartupDetails();
    CoreContext& ctx_;
    QQmlEngine* engine_;
    UiPrefs& prefs_;
    ListedModPtr mod_;
    ModInfoBanner* banner_;
    QLabel* placeholder_;
    QQuickWidget* qml_;
    QString description_;
    quint64 previewToken_ = 0;
    quint64 startupToken_ = 0;
    std::optional<QString> startupPid_;
    QStringList startupActivePids_;
    QPointer<StartupImpactDialog> startupDialog_;
    Event<bool>::Id busyConnection_;
};
}
