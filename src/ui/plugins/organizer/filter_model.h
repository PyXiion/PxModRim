#pragma once

#include <QAbstractListModel>

#include "core/organizer/resolve.h"

namespace px::modrim {

class OrganizerFilterModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { LabelRole = Qt::UserRole + 1, CountRole, IconRole, SectionRole, KeyRole };
    Q_ENUM(Role)
    explicit OrganizerFilterModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void update(const QList<organizer::TreeFilter>& entries);
    std::optional<QString> key_at(int row) const;
    organizer::TreeFilter for_key(const QString& key) const;

private:
    QList<organizer::TreeFilter> items_;
};

}  // namespace px::modrim
