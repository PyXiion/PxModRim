#pragma once
#include <vector>
#include <QCoro/QCoroTask>
#include <QDialog>
#include <QVariantMap>
#include "core/context.h"
#include "ui/settings_section.h"
class QQmlEngine;
class QQuickWidget;
namespace px::modrim {
class AppContext;
class SettingsBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap initial READ initial CONSTANT)
    Q_PROPERTY(QVariantList sections READ sections CONSTANT)
    Q_PROPERTY(QString communityStatus READ communityStatus NOTIFY communityChanged)
    Q_PROPERTY(bool communityBusy READ communityBusy NOTIFY communityChanged)
    Q_PROPERTY(QString cacheStatus READ cacheStatus NOTIFY cacheChanged)
    Q_PROPERTY(bool cacheAvailable READ cacheAvailable CONSTANT)
    Q_PROPERTY(bool cacheBusy READ cacheBusy NOTIFY cacheChanged)
public:
    SettingsBackend(CoreContext& ctx, QDialog* dialog, const std::vector<SettingsSection*>& sections);
    QVariantMap initial() const { return initial_; }
    QVariantList sections() const { return sections_; }
    QString communityStatus() const { return communityStatus_; }
    bool communityBusy() const { return communityBusy_; }
    QString cacheStatus() const { return cacheStatus_; }
    bool cacheAvailable() const { return cacheAvailable_; }
    bool cacheBusy() const { return cacheBusy_; }
    Q_INVOKABLE QCoro::Task<void> browse(QString key);
    Q_INVOKABLE QString localCandidate(const QString& gamePath) const;
    Q_INVOKABLE void autoDetect();
    Q_INVOKABLE QCoro::Task<void> downloadCommunityRules();
    Q_INVOKABLE QCoro::Task<void> clearCache();
    Q_INVOKABLE void save(const QVariantMap& values) { emit saveRequested(values); }
    Q_INVOKABLE void cancel() { emit cancelRequested(); }
    void reportSaveError(const QString& message);
signals:
    void pathPicked(const QString& key, const QString& path);
    void saveRequested(const QVariantMap& values);
    void cancelRequested();
    void communityChanged();
    void cacheChanged();
private:
    CoreContext& ctx_;
    QDialog* dialog_;
    QVariantMap initial_;
    QVariantList sections_;
    QString communityStatus_, cacheStatus_;
    bool communityBusy_ = false, cacheBusy_ = false, cacheAvailable_ = false;
};
class SettingsPanel : public QDialog {
    Q_OBJECT
public:
    SettingsPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
                  const std::vector<SettingsSectionFactory>& sections = {});
    SettingsPanel(AppContext& app, QQmlEngine* engine, QWidget* parent);
    ~SettingsPanel() override;
    const AppConfig& getConfig() const { return config_; }
private:
    void save(const QVariantMap& values);
    AppConfig config_;
    std::vector<SettingsSection*> sections_;
    SettingsBackend* backend_;
    QQuickWidget* qml_;
};
} // namespace px::modrim
