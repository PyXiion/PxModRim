#pragma once

#include <QAbstractListModel>

#include "core/organizer/resolve.hpp"

namespace px::modrim {

class OrganizerFilterModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { kLabelRole = Qt::UserRole + 1, kCountRole, kIconRole, kSectionRole, kKeyRole };
    Q_ENUM(Role)
    explicit OrganizerFilterModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void update(const QList<organizer::TreeFilter>& entries);
    std::optional<QString> keyAt(int row) const;
    organizer::TreeFilter forKey(const QString& key) const;

private:
    QList<organizer::TreeFilter> m_items;
};

}  // namespace px::modrim
