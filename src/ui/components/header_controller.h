#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace px::ui {

// State and commands behind Header.qml, exposed to it as `headerController`. The
// QML-facing names (properties and invokables) match the legacy HeaderController;
// the C++ side uses camelCase signals and setters. Owned by the caller.
class HeaderController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool is_frameless READ isFrameless CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QVariantMap tooltips READ tooltips CONSTANT)
    Q_PROPERTY(bool downloadsAvailable READ downloadsAvailable CONSTANT)
    Q_PROPERTY(QVariantList strategies READ strategies CONSTANT)

    Q_PROPERTY(bool maximized READ maximized WRITE setMaximized NOTIFY maximizedChanged)
    Q_PROPERTY(int strategyIndex READ strategyIndex NOTIFY strategyChanged)
    Q_PROPERTY(bool unsavedChanges READ unsavedChanges WRITE setUnsavedChanges NOTIFY unsavedChangesChanged)
    Q_PROPERTY(QString launchState READ launchState WRITE setLaunchState NOTIFY launchStateChanged)
    Q_PROPERTY(QString taskText READ taskText NOTIFY taskTextChanged)
    Q_PROPERTY(bool downloadsBusy READ downloadsBusy WRITE setDownloadsBusy NOTIFY downloadsBusyChanged)
    Q_PROPERTY(bool downloadsProgressShown READ downloadsProgressShown NOTIFY downloadsProgressShownChanged)
    Q_PROPERTY(QString downloadsText READ downloadsText NOTIFY downloadsProgressChanged)
    Q_PROPERTY(int downloadsValue READ downloadsValue NOTIFY downloadsProgressChanged)
    Q_PROPERTY(int downloadsTotal READ downloadsTotal NOTIFY downloadsProgressChanged)

public:
    HeaderController(bool isFrameless, int initialStrategy, QString appVersion, QVariantMap tooltips,
                     bool downloadsAvailable, QObject* parent = nullptr);

    bool isFrameless() const { return is_frameless_; }
    const QString& appVersion() const { return app_version_; }
    const QVariantMap& tooltips() const { return tooltips_; }
    bool downloadsAvailable() const { return downloads_available_; }
    // [{index, label}] for each launch strategy, in enum order.
    QVariantList strategies() const;

    bool maximized() const { return maximized_; }
    int strategyIndex() const { return strategy_index_; }
    bool unsavedChanges() const { return unsaved_changes_; }
    const QString& launchState() const { return launch_state_; }
    const QString& taskText() const { return task_text_; }
    bool downloadsBusy() const { return downloads_busy_; }
    bool downloadsProgressShown() const { return downloads_progress_shown_; }
    const QString& downloadsText() const { return downloads_text_; }
    int downloadsValue() const { return downloads_value_; }
    int downloadsTotal() const { return downloads_total_; }

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
    // Called by QML when a drag starts on the header background. Emits dragStartRequested.
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
    bool is_frameless_;
    QString app_version_;
    QVariantMap tooltips_;
    bool downloads_available_;

    bool maximized_ = false;
    int strategy_index_;
    bool unsaved_changes_ = false;
    QString launch_state_ = QStringLiteral("idle");
    QString task_text_;
    bool downloads_busy_ = false;
    bool downloads_progress_shown_ = true;
    QString downloads_text_;
    int downloads_value_ = 0;
    int downloads_total_ = 0;
};

}  // namespace px::ui
