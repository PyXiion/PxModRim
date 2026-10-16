#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <cstdint>
#include "core/models/metadata/structures.hpp"
#include "core/models/view/diagnostics.hpp"

namespace px::modrim { class DiagnosticsService; }
namespace px::modrim {
QString provider_label(const QString& provider_id, const QString& package_id = {});
QString section_name(bool checked, int active_count, int inactive_count);

struct ModItem {
    ListedModPtr mod;
    QString uuid;
    bool checked = false;
    QString provider_color;
    double startup_impact_s = 0;
    int load_index = -1;
    QString provider_label;
};

class ModListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int activeCount READ active_count NOTIFY active_count_changed)
public:
    enum Role {
        CheckStateRole = Qt::UserRole + 1, PackageIdRole, ModVersionRole, ProviderColorRole,
        UuidRole, HasErrorRole, HasWarningRole, ErrorTooltipRole, WarningTooltipRole,
        StartupImpactRole, LoadIndexRole, IsActiveRole, SectionNameRole, ProviderLabelRole,
        StartupImpactColorRole, StartupImpactTextRole
    };
    Q_ENUM(Role)
    explicit ModListModel(QHash<QString, QString> provider_colors = {},
                          DiagnosticsService* diagnostics = nullptr, QObject* parent = nullptr);
    ~ModListModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;
    void update_provider_colors(QHash<QString, QString> colors);
    void load_mods(const QHash<QString, ListedModPtr>& mods, const QStringList& active_uuids);
    QStringList active_uuids() const;
    int active_count() const { return active_count_; }
    const ModItem* get_item(int row) const;
    const ModItem* get_item_by_uuid(const QString& uuid) const;
    bool move_row(int source_row, int target_row);
    void commitOrder(const QStringList& uuids);
    void set_checkable(const QSet<QString>& uuids, bool checked);
    void set_check_states(const QList<int>& rows, bool checked);
    void set_startup_impact(const QHash<QString, double>& impacts = {});
signals:
    void active_mods_changed();
    void active_count_changed();
private:
    QList<int> update_load_indices();
    ModItem make_item(const QString& uuid, ListedModPtr mod, bool checked) const;
    void diagnostics_changed(const QHash<QString, view::ModDiagnosticsView>& summaries);
    QHash<QString, QString> provider_colors_;
    QList<ModItem> items_;
    int active_count_ = 0;
    DiagnosticsService* diagnostics_;
    std::uint64_t diagnostics_connection_ = 0;
    QHash<QString, view::ModDiagnosticsView> last_diagnostics_;
};
}
