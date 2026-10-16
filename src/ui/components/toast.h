#pragma once

#include <QString>
#include <QWidget>

#include "ui/context.h"

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QVBoxLayout;

namespace px::modrim {

enum class ToastLevel { Info, Success, Warning, Error };

// One message card. Fades in on show; dismiss() fades it out and then emits dismissed().
class Toast : public QWidget {
    Q_OBJECT

public:
    Toast(const QString& message, ToastLevel level, QWidget* parent = nullptr);

    void showWithAnimation();
    void dismiss();

Q_SIGNALS:
    void dismissed();

private:
    QGraphicsOpacityEffect* opacity_effect_ = nullptr;
    QPropertyAnimation* opacity_anim_ = nullptr;
    QPropertyAnimation* fade_out_anim_ = nullptr;
    bool dismissing_ = false;
};

// Stacks toasts bottom-right over its parent. Durations are milliseconds; 0 keeps the toast
// until its close button is clicked.
class ToastManager : public QWidget, public Notifier {
    Q_OBJECT

public:
    explicit ToastManager(QWidget* parent = nullptr);

    void showToast(const QString& message, ToastLevel level = ToastLevel::Info, int durationMs = 3000);

    void info(const QString& message, int durationMs = 3000) override;
    void success(const QString& message, int durationMs = 3000) override;
    void warning(const QString& message, int durationMs = 4000) override;
    void error(const QString& message, int durationMs = 5000) override;

    // Covers the parent's rectangle; call on every parent resize.
    void resizeToParent();

private:
    void remove(Toast* toast);

    QVBoxLayout* layout_ = nullptr;
};

}  // namespace px::modrim
