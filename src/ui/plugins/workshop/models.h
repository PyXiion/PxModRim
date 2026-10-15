#pragma once

#include <functional>
#include <optional>

#include <QAbstractListModel>
#include <QVariantMap>

#include "core/workshop/types.h"

namespace px::ui::workshop {

using core::workshop::CatalogEntry;

QString safe_url(const QString& value);
QString summarize(const QString& text, const QString& format = QStringLiteral("bbcode"));
QVariantMap item_row(const CatalogEntry& item,
                     const std::function<core::workshop::InstallState(const core::workshop::CatalogMod&)>& install_state,
                     const QString& game_version, bool detail = false);

class CatalogListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)

public:
    explicit CatalogListModel(QObject* parent = nullptr);

    void set_page(QList<QVariantMap> rows, int total, std::optional<QString> cursor, bool append = false);
    void refresh_rows(const std::function<QVariantMap(const QVariantMap&)>& convert);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    const std::optional<QString>& cursor() const { return cursor_; }
    bool hasMore() const { return cursor_.has_value(); }
    int count() const { return static_cast<int>(rows_.size()); }
    int total() const { return total_; }
    const QList<QVariantMap>& rows() const { return rows_; }

signals:
    void changed();

private:
    QList<QVariantMap> rows_;
    std::optional<QString> cursor_;
    int total_ = 0;
};

}  // namespace px::ui::workshop
