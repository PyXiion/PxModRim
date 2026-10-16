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
    QString editorError() const { return m_editorError; }
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
    QCoro::Task<void> editFolder(QString action, qint64 id = organizer::kRootId, QStringList pids = {});
    QCoro::Task<void> placeMods(QStringList, qint64);
    QCoro::Task<void> resetRules(QStringList);
    QCoro::Task<void> performDrop(QModelIndex, QModelIndex);
    void modMenu();
    void folderMenu(const TreeNode&);

    organizer::OrganizerService& m_service;
    ModTreeModel* m_model;
    OrganizerFilterModel* m_filters;
    ModInfoPanel* m_info;
    std::unique_ptr<ModSelectionPresenter> m_selection;
    QQuickWidget* m_qml;
    QQuickWidget* m_rulesQml;
    QDialog* m_rulesDialog;
    QLineEdit* m_search;
    QLabel* m_status;
    QLabel* m_selectionLabel;
    QWidget* m_selectionBar;
    QPushButton* m_updateButton;
    QMenu* m_menu;
    QTimer* m_searchTimer;
    QPersistentModelIndex m_selectionAnchor;
    QString m_selectedUuid;
    QString m_filterKey = QStringLiteral("all");
    QString m_searchText;
    QString m_editorError;
    bool m_dirty = true;
    bool m_rulesOpenedOnce = false;
    std::uint64_t m_changedId = 0;
    std::uint64_t m_activeId = 0;
    std::uint64_t m_modsId = 0;
    std::uint64_t m_diagnosticsId = 0;
    std::uint64_t m_busyId = 0;
};
}
