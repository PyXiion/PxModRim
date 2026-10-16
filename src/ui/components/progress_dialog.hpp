#pragma once

#include <QDialog>
#include <QMetaObject>
#include <QStringList>
#include <vector>

#include "core/loading.hpp"

class QHBoxLayout;
class QProgressBar;
class QPropertyAnimation;
class QTimer;
class QWidget;

namespace px::modrim {

// Modal progress dialog showing the stacked loading tasks as breadcrumbs. Shows
// when the first task reports, and fades out shortly after the last one
// finishes unless a new task starts.
class ProgressDialog : public QDialog {
  Q_OBJECT

 public:
  ProgressDialog(LoadingState& loading, QWidget* parent);
  ~ProgressDialog() override;

  LoadingState& loading() const noexcept { return m_loading; }

 private:
  void onChanged(int pct);
  void onFinished();
  void updateBreadcrumbs(const QStringList& statuses);
  void beginHide();
  void fadeTo(qreal end, int durationMs);
  void onFadeFinished();

  LoadingState& m_loading;
  bool m_shown = false;

  QWidget* m_crumbs = nullptr;
  QHBoxLayout* m_crumbsLayout = nullptr;
  QProgressBar* m_progress = nullptr;
  QTimer* m_hideTimer = nullptr;
  QPropertyAnimation* m_fade = nullptr;

  std::vector<QMetaObject::Connection> m_connections;
};

}  // namespace px::modrim
