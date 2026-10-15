#pragma once

#include <vector>

#include <QDialog>
#include <QMetaObject>
#include <QStringList>

#include "core/loading.h"

class QHBoxLayout;
class QProgressBar;
class QPropertyAnimation;
class QTimer;
class QWidget;

namespace px::ui {

// Modal progress dialog showing the stacked loading tasks as breadcrumbs. Shows when the first
// task reports, and fades out shortly after the last one finishes unless a new task starts.
class ProgressDialog : public QDialog {
    Q_OBJECT

public:
    ProgressDialog(core::LoadingState& loading, QWidget* parent);
    ~ProgressDialog() override;

    core::LoadingState& loading() const noexcept { return loading_; }

private:
    void onChanged(int pct);
    void onFinished();
    void updateBreadcrumbs(const QStringList& statuses);
    void beginHide();
    void fadeTo(qreal end, int durationMs);
    void onFadeFinished();

    core::LoadingState& loading_;
    bool shown_ = false;

    QWidget* crumbs_ = nullptr;
    QHBoxLayout* crumbs_layout_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTimer* hide_timer_ = nullptr;
    QPropertyAnimation* fade_ = nullptr;

    std::vector<QMetaObject::Connection> connections_;
};

}  // namespace px::ui
