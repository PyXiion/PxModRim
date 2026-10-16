#pragma once

#include <cstdint>
#include <memory>
#include <QPersistentModelIndex>
#include <QVariantList>
#include <QVariantMap>
#include <QCoro/QCoroQmlTask>
#include <QCoro/QCoroTask>
#include "core/organizer/service.hpp"
#include "ui/views/base.hpp"

class QDialog;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QQuickWidget;
class QTimer;
class QShowEvent;

namespace px::modrim {
class ModTreeModel;
class OrganizerFilterModel;
class ModInfoPanel;
class ModSelectionPresenter;
struct TreeNode;

class OrganizerViewPanel final : public BaseViewPanel {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(int selectedTagCount READ selectedTagCount NOTIFY editorChanged)
    Q_PROPERTY(QVariantList tagRows READ tagRows NOTIFY editorChanged)
    Q_PROPERTY(QVariantList ruleRows READ ruleRows NOTIFY editorChanged)
    Q_PROPERTY(QVariantList ruleFolders READ ruleFolders NOTIFY editorChanged)
    Q_PROPERTY(QString editorError READ editorError NOTIFY editorChanged)
public:
    static constexpr RailViewMeta kRailMeta{QLatin1StringView("organizer"), QLatin1StringView("folder"), QLatin1StringView("Organizer")};
    OrganizerViewPanel(CoreContext&, QQmlEngine*, QWidget*, AppContext&);
    ~OrganizerViewPanel() override;
    bool ready() const;
    int selectedTagCount() const;
    QVariantList tagRows() const;
    QVariantList ruleRows() const;
    QVariantList ruleFolders() const;
    QString editorError() const { return editor_error_; }
    Q_INVOKABLE QCoro::QmlTask requestRulePreview(const QVariantList&, int revision);
    Q_INVOKABLE void openTags();
    Q_INVOKABLE void openRules();
    Q_INVOKABLE void closeRules();
    Q_INVOKABLE QCoro::QmlTask createTag(QString name, QString color);
    Q_INVOKABLE QCoro::QmlTask updateTag(qint64 id, QString name, QString color);
    Q_INVOKABLE QCoro::QmlTask deleteTag(qint64 id);
    Q_INVOKABLE QCoro::QmlTask assignTag(qint64 id, bool add);
    Q_INVOKABLE QCoro::QmlTask saveRules(QVariantList rows);
    Q_INVOKABLE QCoro::QmlTask addStandardRules();
    Q_INVOKABLE QVariantList expandedIndexes() const;
    Q_INVOKABLE void selectRow(const QModelIndex&, int modifiers, const QVariantList& range);
    Q_INVOKABLE void selectSingle(const QModelIndex&);
    Q_INVOKABLE QCoro::QmlTask persistAllCollapsed(bool);
    Q_INVOKABLE QCoro::QmlTask persistCollapsed(const QModelIndex&, bool);
    Q_INVOKABLE QCoro::QmlTask toggleCheck(const QModelIndex&);
    Q_INVOKABLE void contextMenu(const QModelIndex&);
    Q_INVOKABLE QCoro::QmlTask selectionAction(const QString&);
    Q_INVOKABLE QCoro::QmlTask newFolder();
    Q_INVOKABLE bool canDrop(const QModelIndex&, const QModelIndex&) const;
    Q_INVOKABLE QCoro::QmlTask dropOn(const QModelIndex&, const QModelIndex&);
    void rebuild();
Q_SIGNALS:
    void readyChanged();
    void editorChanged();
    void tagCreated();
    void tagUpdated(qint64 id);
    void rulesSaved();
    void rulePreviewReady(int revision, const QVariantMap& result);
protected:
    void showEvent(QShowEvent*) override;
private:
    void invalidate();
    void syncInfo(bool force = false);
    void treeCommand(const char*);
    void setEditorError(QString message = {});
    void reportError(const Error&);
    QStringList selectedPackageIds() const;
    QStringList selectedUuids() const;
    Result<QList<organizer::RuleSpec>> ruleSpecs(const QVariantList&) const;
    QCoro::Task<void> calculateRulePreview(QVariantList, int);
    QCoro::Task<void> mutateTag(QString operation, qint64 id, QString name = {}, QString color = {}, bool add = false);
    QCoro::Task<void> persistRules(QVariantList);
    QCoro::Task<void> standardRules();
    QCoro::Task<void> persistCollapse(QModelIndex, bool);
    QCoro::Task<void> persistAllCollapse(bool);
    QCoro::Task<void> toggle(QModelIndex);
    QCoro::Task<void> runSelectionAction(QString);
    QCoro::Task<void> editFolder(QString action, qint64 id = organizer::ROOT_ID, QStringList pids = {});
    QCoro::Task<void> placeMods(QStringList, qint64);
    QCoro::Task<void> resetRules(QStringList);
    QCoro::Task<void> performDrop(QModelIndex, QModelIndex);
    void modMenu();
    void folderMenu(const TreeNode&);

    organizer::OrganizerService& service_;
    ModTreeModel* model_;
    OrganizerFilterModel* filters_;
    ModInfoPanel* info_;
    std::unique_ptr<ModSelectionPresenter> selection_;
    QQuickWidget* qml_;
    QQuickWidget* rules_qml_;
    QDialog* rules_dialog_;
    QLineEdit* search_;
    QLabel* status_;
    QLabel* selection_label_;
    QWidget* selection_bar_;
    QPushButton* update_button_;
    QMenu* menu_;
    QTimer* search_timer_;
    QPersistentModelIndex selection_anchor_;
    QString selected_uuid_;
    QString filter_key_ = QStringLiteral("all");
    QString search_text_;
    QString editor_error_;
    bool dirty_ = true;
    bool rules_opened_once_ = false;
    std::uint64_t changed_id_ = 0;
    std::uint64_t active_id_ = 0;
    std::uint64_t mods_id_ = 0;
    std::uint64_t diagnostics_id_ = 0;
    std::uint64_t busy_id_ = 0;
};
}
