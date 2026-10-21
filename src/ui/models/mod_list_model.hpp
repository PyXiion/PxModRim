#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <cstdint>
#include <utility>

#include "core/models/metadata/structures.hpp"
#include "core/models/view/diagnostics.hpp"

namespace px::modrim {
class DiagnosticsService;
}
namespace px::modrim {
QString providerLabel(const QString& providerId, const QString& packageId = {});
QString sectionName(bool checked, int activeCount, int inactiveCount);

struct ModItem {
  ListedModPtr mod;
  QString uuid;
  bool checked = false;
  QString providerColor;
  double startupImpactS = 0;
  int loadIndex = -1;
  QString providerLabel;
};

class ModListModel : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Role {
    kCheckStateRole = Qt::UserRole + 1,
    kPackageIdRole,
    kModVersionRole,
    kProviderColorRole,
    kUuidRole,
    kHasErrorRole,
    kHasWarningRole,
    kErrorTooltipRole,
    kWarningTooltipRole,
    kStartupImpactRole,
    kLoadIndexRole,
    kIsActiveRole,
    kSectionNameRole,
    kProviderLabelRole,
    kStartupImpactColorRole,
    kStartupImpactTextRole
  };
  Q_ENUM(Role)
  explicit ModListModel(QHash<QString, QString> providerColors = {},
                        DiagnosticsService* diagnostics = nullptr,
                        QObject* parent = nullptr);
  ~ModListModel() override;
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex& index, const QVariant& value,
               int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QHash<int, QByteArray> roleNames() const override;
  void updateProviderColors(QHash<QString, QString> colors);
  void loadMods(const QHash<QString, ListedModPtr>& mods,
                const QStringList& activeUuids);
  QStringList activeUuids() const;
  const ModItem* getItem(int row) const;
  bool moveRow(int sourceRow, int targetRow);
  void commitOrder(const QStringList& uuids);
  void setCheckable(const QSet<QString>& uuids, bool checked);
  void setCheckStates(const QList<int>& rows, bool checked);
  void setStartupImpact(const QHash<QString, double>& impacts);

 private:
  std::pair<int, int> updateLoadIndices();
  ModItem makeItem(const QString& uuid, ListedModPtr mod, bool checked) const;
  void diagnosticsChanged(
      const QHash<QString, view::ModDiagnosticsView>& summaries);
  QHash<QString, QString> m_providerColors;
  QList<ModItem> m_items;
  int m_activeCount = 0;
  DiagnosticsService* m_diagnostics;
  std::uint64_t m_diagnosticsConnection = 0;
  QHash<QString, view::ModDiagnosticsView> m_lastDiagnostics;
};
}  // namespace px::modrim
