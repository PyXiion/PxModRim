#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include "core/downloads/types.hpp"

namespace px::modrim {

class DownloadsModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int total READ total NOTIFY summaryChanged)
  Q_PROPERTY(int finished READ finished NOTIFY summaryChanged)
  Q_PROPERTY(int updated READ updated NOTIFY summaryChanged)
  Q_PROPERTY(int unchanged READ unchanged NOTIFY summaryChanged)
  Q_PROPERTY(int failed READ failed NOTIFY summaryChanged)
  Q_PROPERTY(int active READ active NOTIFY summaryChanged)
  Q_PROPERTY(int resolved READ resolved NOTIFY summaryChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY summaryChanged)
  Q_PROPERTY(QString filter READ filter NOTIFY summaryChanged)
  Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
  Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
  Q_PROPERTY(double bytesDone READ bytesDone NOTIFY speedChanged)
  Q_PROPERTY(QVariantList speedHistory READ speedHistory NOTIFY speedChanged)

 public:
  enum Role {
    kIdRole = Qt::UserRole + 1,
    kTitleRole,
    kStateRole,
    kProgressRole,
    kErrorRole
  };
  static constexpr qsizetype kHistoryLength = 90;

  explicit DownloadsModel(QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void begin(const QStringList& ids, const QHash<QString, QString>& titles);
  void apply(const DownloadItemStatus& status);
  void setTitle(const DownloadItemTitle& item);
  void setPhase(const QString& phase);
  void finish(const DownloadResult& result);
  void setBusy(bool busy);
  QStringList failedIds() const;
  void flush();
  Q_INVOKABLE void clear();
  Q_INVOKABLE void setFilter(const QString& name);

  int total() const { return static_cast<int>(m_rows.size()); }
  int finished() const { return updated() + unchanged() + failed(); }
  int updated() const { return m_counts.value(QStringLiteral("updated")); }
  int unchanged() const { return m_counts.value(QStringLiteral("unchanged")); }
  int failed() const {
    return m_counts.value(QStringLiteral("failed")) +
           m_counts.value(QStringLiteral("cancelled"));
  }
  int active() const {
    return m_counts.value(QStringLiteral("queued")) +
           m_counts.value(QStringLiteral("checking")) +
           m_counts.value(QStringLiteral("downloading"));
  }
  int resolved() const {
    return total() - m_counts.value(QStringLiteral("queued"));
  }
  bool busy() const { return m_busy; }
  QString filter() const { return m_filter; }
  QString phase() const { return m_phase; }
  double speed() const { return m_speed; }
  double bytesDone() const { return static_cast<double>(m_bytes); }
  QVariantList speedHistory() const { return m_history; }

 signals:
  void summaryChanged();
  void speedChanged();
  void phaseChanged();

 private:
  struct Row {
    QString id;
    QString title;
    QString state = QStringLiteral("queued");
    double progress = 0;
    QString error;
    qint64 bytesDone = 0;
  };
  void mark(const Row& row);
  bool matches(const Row& row) const;
  void select();
  void recount();
  void sampleSpeed();
  QList<Row> m_rows;
  QHash<QString, qsizetype> m_byId;
  QList<qsizetype> m_visible;
  QString m_filter = QStringLiteral("all");
  QString m_phase;
  bool m_busy = false;
  QHash<QString, int> m_counts;
  QSet<QString> m_dirty;
  QTimer m_timer;
  QTimer m_speedTimer;
  qint64 m_bytes = 0;
  qint64 m_sampledBytes = 0;
  double m_speed = 0;
  QVariantList m_history;
};

}  // namespace px::modrim
