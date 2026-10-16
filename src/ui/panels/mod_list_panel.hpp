#pragma once
#include <QCoro/QCoroQmlTask>
#include <QWidget>
#include <QSet>
#include <QTimer>
#include <cstdint>
#include "ui/models/mod_list_proxy_model.hpp"
class QQmlEngine;
class QQuickWidget;
class QLineEdit;
namespace px::modrim { class CoreContext; }
namespace px::modrim {
class ModListPanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool compactMode READ compactMode WRITE setCompactMode NOTIFY compactModeChanged)
    Q_PROPERTY(int highlightRevision READ highlightRevision NOTIFY highlightedChanged)
public:
    ModListPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent = nullptr);
    ~ModListPanel() override;
    void loadMods(const QHash<QString, ListedModPtr>& mods, const QStringList& active);
    QStringList activeUuids() const;
    ModListModel* model() const { return m_model; }
    ModListProxyModel* proxyModel() const { return m_proxy; }
    QQuickWidget* qmlWidget() const { return m_qml; }
    void setSidebarFilter(std::optional<QSet<QString>> uuids);
    void setSearchFilter(const QString& text);
    bool selectUuid(const QString& uuid);
    bool compactMode() const { return m_compactMode; }
    int highlightRevision() const { return m_highlightRevision; }
    void highlightRows(const QStringList& uuids);
    QCoro::Task<void> toggleRows(QList<int> rows);
    QCoro::Task<void> refresh();
    QLineEdit* searchInput;
    Q_INVOKABLE void setCompactMode(bool enabled);
    Q_INVOKABLE bool isHighlighted(const QString& uuid) const;
    Q_INVOKABLE void modSelected(const QString& uuid);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QString uuidAt(int row) const;
    Q_INVOKABLE int rowForUuid(const QString& uuid) const;
    Q_INVOKABLE QCoro::QmlTask toggleCheck(int row) { return toggleRows({row}); }
    Q_INVOKABLE QCoro::QmlTask toggleChecked(const QVariantList& rows);
    Q_INVOKABLE void showContextMenu(const QVariantList& uuids);
    Q_INVOKABLE bool isChecked(int row) const;
    Q_INVOKABLE void commitOrder(const QStringList& uuids);
    Q_INVOKABLE void moveRow(int source, int target);
    Q_INVOKABLE void dragEnded();
    Q_INVOKABLE void selectionChanged(const QVariantList& uuids);
signals:
    void modPicked(const QString& uuid);
    void activeModsChanged();
    void orderChanged();
    void selectionUpdated(const QStringList& uuids);
    void compactModeChanged(bool enabled);
    void highlightedChanged();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    int proxyToSourceRow(int row) const;
    void coreStateChanged(const QStringList& uuids);
    void setHighlighted(const QStringList& uuids);
    CoreContext& m_ctx;
    ModListModel* m_model;
    ModListProxyModel* m_proxy;
    QQuickWidget* m_qml;
    bool m_compactMode;
    QSet<QString> m_highlighted;
    int m_highlightRevision = 0;
    QTimer* m_highlightTimer;
    std::uint64_t m_activeConnection, m_configConnection, m_modsConnection;
};
}
