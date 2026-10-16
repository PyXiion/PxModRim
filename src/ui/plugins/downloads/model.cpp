#include "ui/plugins/downloads/model.hpp"

namespace px::modrim {
namespace {
bool is_active(const QString& state) {
    return state == QLatin1String("queued") || state == QLatin1String("checking") || state == QLatin1String("downloading");
}
QString final_state(qint64 total) {
    return total > 0 ? QStringLiteral("updated") : QStringLiteral("unchanged");
}
}

DownloadsModel::DownloadsModel(QObject* parent)
    : QAbstractListModel(parent), timer_(this), speed_timer_(this) {
    timer_.setSingleShot(true);
    timer_.setInterval(150);
    connect(&timer_, &QTimer::timeout, this, &DownloadsModel::flush);
    speed_timer_.setInterval(1000);
    connect(&speed_timer_, &QTimer::timeout, this, &DownloadsModel::sample_speed);
}

void DownloadsModel::begin(const QStringList& ids, const QHash<QString, QString>& titles) {
    timer_.stop();
    dirty_.clear();
    bytes_ = sampled_bytes_ = 0;
    speed_ = 0;
    history_.clear();
    emit speed_changed();
    set_phase({});
    beginResetModel();
    rows_.clear();
    by_id_.clear();
    rows_.reserve(ids.size());
    for (const auto& id : ids) {
        by_id_.insert(id, rows_.size());
        rows_.append(Row{id, titles.value(id, id)});
    }
    recount();
    select();
    endResetModel();
    emit summary_changed();
}

void DownloadsModel::apply(const DownloadItemStatus& status) {
    const auto found = by_id_.constFind(status.mod_id);
    if (found == by_id_.cend()) return;
    auto& row = rows_[*found];
    const auto done = status.status == QLatin1String("error") ? row.bytes_done : status.bytes_done;
    bytes_ += done - row.bytes_done;
    row.bytes_done = done;
    if (status.status == QLatin1String("error")) {
        row.state = QStringLiteral("failed");
        row.error = status.error;
        row.progress = 0;
    } else if (status.status == QLatin1String("success")) {
        row.state = final_state(status.bytes_total);
        row.progress = 1;
    } else if (status.bytes_total > 0 && status.bytes_done < status.bytes_total) {
        row.state = QStringLiteral("downloading");
        row.progress = static_cast<double>(status.bytes_done) / status.bytes_total;
    } else {
        row.state = final_state(status.bytes_total);
        row.progress = 1;
    }
    mark(row);
}

void DownloadsModel::set_title(const DownloadItemTitle& item) {
    const auto found = by_id_.constFind(item.mod_id);
    if (found == by_id_.cend()) return;
    auto& row = rows_[*found];
    if (row.title == row.id) row.title = item.title;
    if (row.state == QLatin1String("queued")) row.state = QStringLiteral("checking");
    mark(row);
}

void DownloadsModel::set_phase(const QString& phase) {
    if (phase_ == phase) return;
    phase_ = phase;
    emit phase_changed();
}

void DownloadsModel::finish(const DownloadResult&) {
    for (auto& row : rows_) {
        if (is_active(row.state)) {
            row.state = QStringLiteral("cancelled");
            row.progress = 0;
            dirty_.insert(row.id);
        }
    }
    timer_.stop();
    flush();
}

void DownloadsModel::set_busy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    if (busy) {
        sampled_bytes_ = bytes_;
        speed_timer_.start();
    } else {
        speed_timer_.stop();
        speed_ = 0;
        emit speed_changed();
    }
    emit summary_changed();
}

QStringList DownloadsModel::failed_ids() const {
    QStringList ids;
    for (const auto& row : rows_) {
        if (row.state == QLatin1String("failed") || row.state == QLatin1String("cancelled")) ids.append(row.id);
    }
    return ids;
}

void DownloadsModel::clear() {
    if (!busy_) begin({}, {});
}

void DownloadsModel::setFilter(const QString& name) {
    if (filter_ == name) return;
    filter_ = name;
    beginResetModel();
    select();
    endResetModel();
    emit summary_changed();
}

void DownloadsModel::sample_speed() {
    speed_ = static_cast<double>(bytes_ - sampled_bytes_);
    sampled_bytes_ = bytes_;
    if (history_.size() == HistoryLength) history_.removeFirst();
    history_.append(speed_);
    emit speed_changed();
}

void DownloadsModel::mark(const Row& row) {
    dirty_.insert(row.id);
    if (!timer_.isActive()) timer_.start();
}

bool DownloadsModel::matches(const Row& row) const {
    if (filter_ == QLatin1String("active")) return is_active(row.state);
    if (filter_ == QLatin1String("updated")) return row.state == QLatin1String("updated");
    if (filter_ == QLatin1String("failed")) return row.state == QLatin1String("failed") || row.state == QLatin1String("cancelled");
    return true;
}

void DownloadsModel::select() {
    visible_.clear();
    for (qsizetype pos = 0; pos < rows_.size(); ++pos) {
        if (matches(rows_[pos])) visible_.append(pos);
    }
}

void DownloadsModel::recount() {
    counts_.clear();
    for (const auto& row : rows_) ++counts_[row.state];
}

void DownloadsModel::flush() {
    if (dirty_.isEmpty()) return;
    recount();
    if (filter_ == QLatin1String("all")) {
        for (qsizetype pos = 0; pos < visible_.size(); ++pos) {
            if (dirty_.contains(rows_[visible_[pos]].id)) {
                const auto idx = index(static_cast<int>(pos));
                emit dataChanged(idx, idx);
            }
        }
    } else {
        beginResetModel();
        select();
        endResetModel();
    }
    dirty_.clear();
    emit summary_changed();
}

int DownloadsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

QVariant DownloadsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= visible_.size()) return {};
    const auto& row = rows_[visible_[index.row()]];
    switch (role) {
    case IdRole: return row.id;
    case TitleRole: return row.title;
    case StateRole: return row.state;
    case ProgressRole: return row.progress;
    case ErrorRole: return row.error;
    default: return {};
    }
}

QHash<int, QByteArray> DownloadsModel::roleNames() const {
    return {{IdRole, "id"}, {TitleRole, "title"}, {StateRole, "state"}, {ProgressRole, "progress"}, {ErrorRole, "error"}};
}

}  // namespace px::modrim
