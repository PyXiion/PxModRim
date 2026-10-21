#include "ui/plugins/downloads/model.hpp"

namespace px::modrim {
namespace {
bool isActive(const QString& state) {
  return state == QLatin1String("queued") ||
         state == QLatin1String("checking") ||
         state == QLatin1String("downloading");
}
QString finalState(qint64 total) {
  return total > 0 ? QStringLiteral("updated") : QStringLiteral("unchanged");
}
}  // namespace

DownloadsModel::DownloadsModel(QObject* parent)
    : QAbstractListModel(parent), m_timer(this), m_speedTimer(this) {
  m_timer.setSingleShot(true);
  m_timer.setInterval(150);
  connect(&m_timer, &QTimer::timeout, this, &DownloadsModel::flush);
  m_speedTimer.setInterval(1000);
  connect(&m_speedTimer, &QTimer::timeout, this, &DownloadsModel::sampleSpeed);
}

void DownloadsModel::begin(const QStringList& ids,
                           const QHash<QString, QString>& titles) {
  m_timer.stop();
  m_dirty.clear();
  m_bytes = m_sampledBytes = 0;
  m_speed = 0;
  m_history.clear();
  emit speedChanged();
  setPhase({});
  beginResetModel();
  m_rows.clear();
  m_byId.clear();
  m_rows.reserve(ids.size());
  for (const auto& id : ids) {
    m_byId.insert(id, m_rows.size());
    m_rows.append(Row{id, titles.value(id, id)});
  }
  recount();
  select();
  endResetModel();
  emit summaryChanged();
}

void DownloadsModel::apply(const DownloadItemStatus& status) {
  const auto found = m_byId.constFind(status.modId);
  if (found == m_byId.cend()) return;
  auto& row = m_rows[*found];
  const auto done = status.status == QLatin1String("error") ? row.bytesDone
                                                            : status.bytesDone;
  m_bytes += done - row.bytesDone;
  row.bytesDone = done;
  if (status.status == QLatin1String("error")) {
    row.state = QStringLiteral("failed");
    row.error = status.error;
    row.progress = 0;
  } else if (status.status != QLatin1String("success") &&
             status.bytesTotal > 0 && status.bytesDone < status.bytesTotal) {
    row.state = QStringLiteral("downloading");
    row.progress = static_cast<double>(status.bytesDone) / status.bytesTotal;
  } else {
    row.state = finalState(status.bytesTotal);
    row.progress = 1;
  }
  mark(row);
}

void DownloadsModel::setTitle(const DownloadItemTitle& item) {
  const auto found = m_byId.constFind(item.modId);
  if (found == m_byId.cend()) return;
  auto& row = m_rows[*found];
  if (row.title == row.id) row.title = item.title;
  if (row.state == QLatin1String("queued"))
    row.state = QStringLiteral("checking");
  mark(row);
}

void DownloadsModel::setPhase(const QString& phase) {
  if (m_phase == phase) return;
  m_phase = phase;
  emit phaseChanged();
}

void DownloadsModel::finish() {
  for (auto& row : m_rows) {
    if (isActive(row.state)) {
      row.state = QStringLiteral("cancelled");
      row.progress = 0;
      m_dirty.insert(row.id);
    }
  }
  m_timer.stop();
  flush();
}

void DownloadsModel::setBusy(bool busy) {
  if (m_busy == busy) return;
  m_busy = busy;
  if (busy) {
    m_sampledBytes = m_bytes;
    m_speedTimer.start();
  } else {
    m_speedTimer.stop();
    m_speed = 0;
    emit speedChanged();
  }
  emit summaryChanged();
}

QStringList DownloadsModel::failedIds() const {
  QStringList ids;
  for (const auto& row : m_rows) {
    if (row.state == QLatin1String("failed") ||
        row.state == QLatin1String("cancelled"))
      ids.append(row.id);
  }
  return ids;
}

void DownloadsModel::clear() {
  if (!m_busy) begin({}, {});
}

void DownloadsModel::setFilter(const QString& name) {
  if (m_filter == name) return;
  m_filter = name;
  beginResetModel();
  select();
  endResetModel();
  emit summaryChanged();
}

void DownloadsModel::sampleSpeed() {
  m_speed = static_cast<double>(m_bytes - m_sampledBytes);
  m_sampledBytes = m_bytes;
  if (m_history.size() == kHistoryLength) m_history.removeFirst();
  m_history.append(m_speed);
  emit speedChanged();
}

void DownloadsModel::mark(const Row& row) {
  m_dirty.insert(row.id);
  if (!m_timer.isActive()) m_timer.start();
}

bool DownloadsModel::matches(const Row& row) const {
  if (m_filter == QLatin1String("active")) return isActive(row.state);
  if (m_filter == QLatin1String("updated"))
    return row.state == QLatin1String("updated");
  if (m_filter == QLatin1String("failed"))
    return row.state == QLatin1String("failed") ||
           row.state == QLatin1String("cancelled");
  return true;
}

void DownloadsModel::select() {
  m_visible.clear();
  for (qsizetype pos = 0; pos < m_rows.size(); ++pos) {
    if (matches(m_rows[pos])) m_visible.append(pos);
  }
}

void DownloadsModel::recount() {
  m_counts.clear();
  for (const auto& row : m_rows) ++m_counts[row.state];
}

void DownloadsModel::flush() {
  if (m_dirty.isEmpty()) return;
  recount();
  if (m_filter == QLatin1String("all")) {
    for (qsizetype pos = 0; pos < m_visible.size(); ++pos) {
      if (m_dirty.contains(m_rows[m_visible[pos]].id)) {
        const auto idx = index(static_cast<int>(pos));
        emit dataChanged(idx, idx);
      }
    }
  } else {
    beginResetModel();
    select();
    endResetModel();
  }
  m_dirty.clear();
  emit summaryChanged();
}

int DownloadsModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

QVariant DownloadsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
    return {};
  const auto& row = m_rows[m_visible[index.row()]];
  switch (role) {
    case kIdRole:
      return row.id;
    case kTitleRole:
      return row.title;
    case kStateRole:
      return row.state;
    case kProgressRole:
      return row.progress;
    case kErrorRole:
      return row.error;
    default:
      return {};
  }
}

QHash<int, QByteArray> DownloadsModel::roleNames() const {
  return {{kIdRole, "id"},
          {kTitleRole, "title"},
          {kStateRole, "state"},
          {kProgressRole, "progress"},
          {kErrorRole, "error"}};
}

}  // namespace px::modrim
