#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace px::modrim {

// State and commands behind Header.qml, exposed to it as `headerController`.
// Owned by the caller.
class HeaderController : public QObject {
  Q_OBJECT

  Q_PROPERTY(bool isFrameless READ isFrameless CONSTANT)
  Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
  Q_PROPERTY(QVariantMap tooltips READ tooltips CONSTANT)
  Q_PROPERTY(bool downloadsAvailable READ downloadsAvailable CONSTANT)
  Q_PROPERTY(QVariantList strategies READ strategies CONSTANT)

  Q_PROPERTY(
      bool maximized READ maximized WRITE setMaximized NOTIFY maximizedChanged)
  Q_PROPERTY(int strategyIndex READ strategyIndex NOTIFY strategyChanged)
  Q_PROPERTY(bool unsavedChanges READ unsavedChanges WRITE setUnsavedChanges
                 NOTIFY unsavedChangesChanged)
  Q_PROPERTY(QString launchState READ launchState WRITE setLaunchState NOTIFY
                 launchStateChanged)
  Q_PROPERTY(QString taskText READ taskText NOTIFY taskTextChanged)
  Q_PROPERTY(bool downloadsBusy READ downloadsBusy WRITE setDownloadsBusy NOTIFY
                 downloadsBusyChanged)
  Q_PROPERTY(bool downloadsProgressShown READ downloadsProgressShown NOTIFY
                 downloadsProgressShownChanged)
  Q_PROPERTY(
      QString downloadsText READ downloadsText NOTIFY downloadsProgressChanged)
  Q_PROPERTY(
      int downloadsValue READ downloadsValue NOTIFY downloadsProgressChanged)
  Q_PROPERTY(
      int downloadsTotal READ downloadsTotal NOTIFY downloadsProgressChanged)

 public:
  HeaderController(bool isFrameless, int initialStrategy, QString appVersion,
                   QVariantMap tooltips, bool downloadsAvailable,
                   QObject* parent = nullptr);

  bool isFrameless() const { return m_isFrameless; }
  const QString& appVersion() const { return m_appVersion; }
  const QVariantMap& tooltips() const { return m_tooltips; }
  bool downloadsAvailable() const { return m_downloadsAvailable; }
  // [{index, label}] for each launch strategy, in enum order.
  QVariantList strategies() const;

  bool maximized() const { return m_maximized; }
  int strategyIndex() const { return m_strategyIndex; }
  bool unsavedChanges() const { return m_unsavedChanges; }
  const QString& launchState() const { return m_launchState; }
  const QString& taskText() const { return m_taskText; }
  bool downloadsBusy() const { return m_downloadsBusy; }
  bool downloadsProgressShown() const { return m_downloadsProgressShown; }
  const QString& downloadsText() const { return m_downloadsText; }
  int downloadsValue() const { return m_downloadsValue; }
  int downloadsTotal() const { return m_downloadsTotal; }

  void setMaximized(bool value);
  void setUnsavedChanges(bool value);
  void setLaunchState(const QString& state);
  void setTaskText(const QString& text);
  void setDownloadsBusy(bool busy);
  void setDownloadsProgressShown(bool shown);
  void setDownloadsProgress(const QString& text, int value, int total);

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void autoSort();
  Q_INVOKABLE void openDownloads();
  Q_INVOKABLE void updateMods();
  Q_INVOKABLE void save();
  Q_INVOKABLE void launch();
  Q_INVOKABLE void minimize();
  Q_INVOKABLE void maximize();
  Q_INVOKABLE void closeWindow();
  Q_INVOKABLE void setStrategy(int index);
  // Called by QML when a drag starts on the header background. Emits
  // dragStartRequested.
  Q_INVOKABLE void dragStarted();

 signals:
  void refreshRequested();
  void sortRequested();
  void saveRequested();
  void launchRequested();
  void strategyChanged(int index);
  void minimizeRequested();
  void maximizeRequested();
  void closeRequested();
  void dragStartRequested();
  void updateModsRequested();
  void downloadsRequested();

  void maximizedChanged();
  void unsavedChangesChanged();
  void launchStateChanged();
  void taskTextChanged();
  void downloadsBusyChanged();
  void downloadsProgressShownChanged();
  void downloadsProgressChanged();

 private:
  bool m_isFrameless;
  QString m_appVersion;
  QVariantMap m_tooltips;
  bool m_downloadsAvailable;

  bool m_maximized = false;
  int m_strategyIndex;
  bool m_unsavedChanges = false;
  QString m_launchState = QStringLiteral("idle");
  QString m_taskText;
  bool m_downloadsBusy = false;
  bool m_downloadsProgressShown = true;
  QString m_downloadsText;
  int m_downloadsValue = 0;
  int m_downloadsTotal = 0;
};

}  // namespace px::modrim
