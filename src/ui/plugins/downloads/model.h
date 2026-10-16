#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include "core/downloads/types.h"

namespace px::modrim {

class DownloadsModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int total READ total NOTIFY summary_changed)
    Q_PROPERTY(int finished READ finished NOTIFY summary_changed)
    Q_PROPERTY(int updated READ updated NOTIFY summary_changed)
    Q_PROPERTY(int unchanged READ unchanged NOTIFY summary_changed)
    Q_PROPERTY(int failed READ failed NOTIFY summary_changed)
    Q_PROPERTY(int active READ active NOTIFY summary_changed)
    Q_PROPERTY(int resolved READ resolved NOTIFY summary_changed)
    Q_PROPERTY(bool busy READ busy NOTIFY summary_changed)
    Q_PROPERTY(QString filter READ filter NOTIFY summary_changed)
    Q_PROPERTY(QString phase READ phase NOTIFY phase_changed)
    Q_PROPERTY(double speed READ speed NOTIFY speed_changed)
    Q_PROPERTY(double bytesDone READ bytesDone NOTIFY speed_changed)
    Q_PROPERTY(QVariantList speedHistory READ speedHistory NOTIFY speed_changed)

public:
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, StateRole, ProgressRole, ErrorRole };
    static constexpr qsizetype HistoryLength = 90;

    explicit DownloadsModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void begin(const QStringList& ids, const QHash<QString, QString>& titles);
    void apply(const DownloadItemStatus& status);
    void set_title(const DownloadItemTitle& item);
    void set_phase(const QString& phase);
    void finish(const DownloadResult& result);
    void set_busy(bool busy);
    QStringList failed_ids() const;
    void flush();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setFilter(const QString& name);

    int total() const { return static_cast<int>(rows_.size()); }
    int finished() const { return updated() + unchanged() + failed(); }
    int updated() const { return counts_.value(QStringLiteral("updated")); }
    int unchanged() const { return counts_.value(QStringLiteral("unchanged")); }
    int failed() const { return counts_.value(QStringLiteral("failed")) + counts_.value(QStringLiteral("cancelled")); }
    int active() const { return counts_.value(QStringLiteral("queued")) + counts_.value(QStringLiteral("checking")) + counts_.value(QStringLiteral("downloading")); }
    int resolved() const { return total() - counts_.value(QStringLiteral("queued")); }
    bool busy() const { return busy_; }
    QString filter() const { return filter_; }
    QString phase() const { return phase_; }
    double speed() const { return speed_; }
    double bytesDone() const { return static_cast<double>(bytes_); }
    QVariantList speedHistory() const { return history_; }

signals:
    void summary_changed();
    void speed_changed();
    void phase_changed();

private:
    struct Row {
        QString id;
        QString title;
        QString state = QStringLiteral("queued");
        double progress = 0;
        QString error;
        qint64 bytes_done = 0;
    };
    void mark(const Row& row);
    bool matches(const Row& row) const;
    void select();
    void recount();
    void sample_speed();
    QList<Row> rows_;
    QHash<QString, qsizetype> by_id_;
    QList<qsizetype> visible_;
    QString filter_ = QStringLiteral("all");
    QString phase_;
    bool busy_ = false;
    QHash<QString, int> counts_;
    QSet<QString> dirty_;
    QTimer timer_;
    QTimer speed_timer_;
    qint64 bytes_ = 0;
    qint64 sampled_bytes_ = 0;
    double speed_ = 0;
    QVariantList history_;
};

}  // namespace px::modrim
