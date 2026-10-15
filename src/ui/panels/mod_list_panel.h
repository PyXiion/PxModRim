#pragma once
#include <QCoro/QCoroQmlTask>
#include <QWidget>
#include <QSet>
#include <QTimer>
#include <cstdint>
#include "ui/models/mod_list_proxy_model.h"
class QQmlEngine;
class QQuickWidget;
class QLineEdit;
namespace px::core { class CoreContext; }
namespace px::ui {
class ModListPanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool compactMode READ compactMode WRITE setCompactMode NOTIFY compact_mode_changed)
    Q_PROPERTY(int highlightRevision READ highlightRevision NOTIFY highlighted_changed)
public:
    ModListPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent = nullptr);
    ~ModListPanel() override;
    void load_mods(const QHash<QString, core::ListedModPtr>& mods, const QStringList& active);
    QStringList active_uuids() const;
    ModListModel* model() const { return model_; }
    ModListProxyModel* proxy_model() const { return proxy_; }
    QQuickWidget* qml_widget() const { return qml_; }
    void set_sidebar_filter(std::optional<QSet<QString>> uuids);
    void set_search_filter(const QString& text);
    bool select_uuid(const QString& uuid);
    bool compactMode() const { return compact_mode_; }
    int highlightRevision() const { return highlight_revision_; }
    void highlight_rows(const QStringList& uuids);
    QCoro::Task<void> toggle_rows(QList<int> rows);
    QCoro::Task<void> refresh();
    QLineEdit* search_input;
    Q_INVOKABLE void setCompactMode(bool enabled);
    Q_INVOKABLE bool isHighlighted(const QString& uuid) const;
    Q_INVOKABLE void modSelected(const QString& uuid);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QString uuidAt(int row) const;
    Q_INVOKABLE int rowForUuid(const QString& uuid) const;
    Q_INVOKABLE QCoro::QmlTask toggleCheck(int row) { return toggle_rows({row}); }
    Q_INVOKABLE QCoro::QmlTask toggleChecked(const QVariantList& rows);
    Q_INVOKABLE void showContextMenu(const QVariantList& uuids);
    Q_INVOKABLE bool isChecked(int row) const;
    Q_INVOKABLE void commitOrder(const QStringList& uuids);
    Q_INVOKABLE void moveRow(int source, int target);
    Q_INVOKABLE void dragEnded();
    Q_INVOKABLE void selectionChanged(const QVariantList& uuids);
signals:
    void mod_selected(const QString& uuid);
    void active_mods_changed();
    void order_changed();
    void selection_changed(const QStringList& uuids);
    void compact_mode_changed(bool enabled);
    void highlighted_changed();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    int proxy_to_source_row(int row) const;
    void core_state_changed(const QStringList& uuids);
    void set_highlighted(const QStringList& uuids);
    core::CoreContext& ctx_;
    ModListModel* model_;
    ModListProxyModel* proxy_;
    QQuickWidget* qml_;
    bool compact_mode_;
    QSet<QString> highlighted_;
    int highlight_revision_ = 0;
    QTimer* highlight_timer_;
    std::uint64_t active_connection_, config_connection_, mods_connection_;
};
}
