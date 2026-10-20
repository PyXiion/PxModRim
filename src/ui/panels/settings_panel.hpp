#pragma once
#include <QDialog>
#include <QVariantMap>
#include <vector>

#include <QCoro/QCoroTask>

#include "core/context.hpp"
#include "ui/settings_section.hpp"
#include "ui/ui_prefs.hpp"
class QQmlEngine;
class QQuickWidget;
namespace px::modrim {
class AppContext;
class SettingsBackend : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap initial READ initial CONSTANT)
  Q_PROPERTY(QVariantList sections READ sections CONSTANT)
  Q_PROPERTY(
      QString communityStatus READ communityStatus NOTIFY communityChanged)
  Q_PROPERTY(bool communityBusy READ communityBusy NOTIFY communityChanged)
  Q_PROPERTY(QString cacheStatus READ cacheStatus NOTIFY cacheChanged)
  Q_PROPERTY(bool cacheAvailable READ cacheAvailable CONSTANT)
  Q_PROPERTY(bool cacheBusy READ cacheBusy NOTIFY cacheChanged)
 public:
  SettingsBackend(CoreContext& ctx, QDialog* dialog,
                  const std::vector<SettingsSection*>& sections,
                  const UiPrefs& prefs);
  QVariantMap initial() const { return m_initial; }
  QVariantList sections() const { return m_sections; }
  QString communityStatus() const { return m_communityStatus; }
  bool communityBusy() const { return m_communityBusy; }
  QString cacheStatus() const { return m_cacheStatus; }
  bool cacheAvailable() const { return m_cacheAvailable; }
  bool cacheBusy() const { return m_cacheBusy; }
  Q_INVOKABLE QCoro::Task<void> browse(QString key);
  Q_INVOKABLE QString localCandidate(const QString& gamePath) const;
  Q_INVOKABLE void autoDetect();
  Q_INVOKABLE QCoro::Task<void> downloadCommunityRules();
  Q_INVOKABLE QCoro::Task<void> clearCache();
  Q_INVOKABLE void save(const QVariantMap& values) {
    emit saveRequested(values);
  }
  Q_INVOKABLE void cancel() { emit cancelRequested(); }
  void reportSaveError(const QString& message);
 signals:
  void pathPicked(const QString& key, const QString& path);
  void saveRequested(const QVariantMap& values);
  void cancelRequested();
  void communityChanged();
  void cacheChanged();

 private:
  CoreContext& m_ctx;
  QDialog* m_dialog;
  QVariantMap m_initial;
  QVariantList m_sections;
  QString m_communityStatus, m_cacheStatus;
  bool m_communityBusy = false, m_cacheBusy = false, m_cacheAvailable = false;
};
class SettingsPanel : public QDialog {
  Q_OBJECT
 public:
  SettingsPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
                const std::vector<SettingsSectionFactory>& sections = {},
                UiPrefs prefs = {});
  SettingsPanel(AppContext& app, QQmlEngine* engine, QWidget* parent);
  ~SettingsPanel() override;
  const AppConfig& getConfig() const { return m_config; }
  const UiPrefs& getUiPrefs() const { return m_uiPrefs; }

 private:
  void save(const QVariantMap& values);
  AppConfig m_config;
  UiPrefs m_uiPrefs;
  std::vector<SettingsSection*> m_sections;
  SettingsBackend* m_backend;
  QQuickWidget* m_qml;
};
}  // namespace px::modrim
