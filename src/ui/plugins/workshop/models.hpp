#pragma once

#include <functional>
#include <optional>

#include <QAbstractListModel>
#include <QVariantMap>

#include "core/workshop/types.hpp"

namespace px::modrim::workshop {

using workshop::CatalogEntry;

QString safeUrl(const QString& value);
QString summarize(const QString& text, const QString& format = QStringLiteral("bbcode"));
QVariantMap itemRow(const CatalogEntry& item,
                     const std::function<workshop::InstallState(const workshop::CatalogMod&)>& installState,
                     const QString& gameVersion, bool detail = false);

class CatalogListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)

public:
    explicit CatalogListModel(QObject* parent = nullptr);

    void setPage(QList<QVariantMap> rows, int total, std::optional<QString> cursor, bool append = false);
    void refreshRows(const std::function<QVariantMap(const QVariantMap&)>& convert);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    const std::optional<QString>& cursor() const { return m_cursor; }
    bool hasMore() const { return m_cursor.has_value(); }
    int count() const { return static_cast<int>(m_rows.size()); }
    int total() const { return m_total; }
    const QList<QVariantMap>& rows() const { return m_rows; }

signals:
    void changed();

private:
    QList<QVariantMap> m_rows;
    std::optional<QString> m_cursor;
    int m_total = 0;
};

}  // namespace px::modrim::workshop
