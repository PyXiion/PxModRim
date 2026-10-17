#pragma once
#include <QPointer>
#include <QVariant>
#include <QWidget>

#include "ui/mod_selection.hpp"
#include "ui/ui_prefs.hpp"
class QQmlEngine;
class QQuickWidget;
class QLabel;
namespace px::modrim {
class ModInfoBanner;
class StartupImpactDialog;
class ModInfoPanel : public QWidget, public ModInfoSink {
  Q_OBJECT
 public:
  ModInfoPanel(CoreContext& ctx, QQmlEngine* engine, UiPrefs& prefs,
               QWidget* parent = nullptr);
  ~ModInfoPanel() override;
  void showMod(const ListedMod& mod,
               const QList<view::ModIssueView>& issues) override;
  void setIssues(const QList<view::ModIssueView>& issues);
  void clear() override;
  QCoro::Task<void> setTimeAnalytics(const std::optional<QString>& pid,
                                     const QStringList& activePids) override;
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
  QCoro::Task<void> loadPreview(std::optional<QString> path, QString title,
                                quint64 token);
  QCoro::Task<void> performUpdate(QString id);
  QCoro::Task<void> showStartupDetails();
  CoreContext& m_ctx;
  QQmlEngine* m_engine;
  UiPrefs& m_prefs;
  ListedModPtr m_mod;
  ModInfoBanner* m_banner;
  QLabel* m_placeholder;
  QQuickWidget* m_qml;
  QString m_description;
  quint64 m_previewToken = 0;
  quint64 m_startupToken = 0;
  std::optional<QString> m_startupPid;
  QStringList m_startupActivePids;
  QPointer<StartupImpactDialog> m_startupDialog;
  Event<bool>::Id m_busyConnection;
};
}  // namespace px::modrim
