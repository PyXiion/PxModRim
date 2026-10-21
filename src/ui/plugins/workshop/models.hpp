#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <optional>

#include "core/workshop/types.hpp"

namespace px::modrim::workshop {

using workshop::CatalogEntry;

enum class CardKind { kMod, kCollection };
enum class CardState { kMissing, kInstalled, kOutdated };
enum class CardAction {
  kDownload,
  kInstalled,
  kUpdate,
  kDownloadAll,
  kCompleteDownload,
  kPreparing,
  kDownloading,
  kQueued,
};

// Everything a card needs from outside its catalog entry, captured once per
// refresh so that each page renders against the same snapshot.
struct CardContext {
  QString gameVersion;
  std::function<workshop::InstallState(const workshop::CatalogMod&)>
      installState;
  QSet<QString> installedIds;
  QSet<QString> activeIds;
  QSet<QString> favouriteIds;
  QSet<QString> preparingIds;
  QSet<QString> downloadingIds;
  QSet<QString> queuedIds;
  QHash<QString, QSet<QString>> plannedMembers;
  QString packId;
  QSet<QString> packIds;
  bool detail = false;
};

struct CatalogCard {
  QString itemId;
  CardKind kind = CardKind::kMod;
  QString title;
  QString author;
  QString previewUrl;
  QStringList collage;
  QString sourceLabel;
  QString versions;
  QString tags;
  CardState state = CardState::kMissing;
  bool active = false;
  CardAction action = CardAction::kDownload;
  bool incompatible = false;
  int memberCount = 0;
  QString workshopUrl;
  QString fileSize;
  QString votes;
  QString votesUp;
  QString votesDown;
  bool queued = false;
  bool favourite = false;
  QString summary;
  QString compatLabel;

  bool operator==(const CatalogCard&) const = default;
};

QString safeUrl(const QString& value);
QString summarize(const QString& text,
                  const QString& format = QStringLiteral("bbcode"));
CatalogCard makeCard(const CatalogEntry& item, const CardContext& context);
QVariantMap cardDetail(const CatalogCard& card, const CatalogEntry& item);

class CatalogListModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
  Q_PROPERTY(int count READ count NOTIFY changed)

 public:
  explicit CatalogListModel(QObject* parent = nullptr);

  void setPage(QList<CatalogCard> cards, int total,
               std::optional<QString> cursor, bool append = false);
  void refresh(const std::function<CatalogCard(const CatalogCard&)>& convert);
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  const std::optional<QString>& cursor() const { return m_cursor; }
  bool hasMore() const { return m_cursor.has_value(); }
  int count() const { return static_cast<int>(m_rows.size()); }
  int total() const { return m_total; }

 signals:
  void changed();

 private:
  QList<CatalogCard> m_rows;
  std::optional<QString> m_cursor;
  int m_total = 0;
};

}  // namespace px::modrim::workshop
