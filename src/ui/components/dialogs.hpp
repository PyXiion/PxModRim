#pragma once

// Modal dialogs as coroutines: `co_await awaitDialog<T>(args...)` shows the dialog and
// resumes with its result code and pointer. The dialog is scheduled for deletion, so
// read what you need from it before the next event-loop pass (the coroutine resumes
// synchronously from `finished`, so that holds for any code after the co_await).

#include <concepts>
#include <coroutine>
#include <utility>

#include <QCoro/QCoroTask>
#include <QDialog>
#include <QObject>

#include "ui/components/dialog_chrome.hpp"

namespace px::modrim {

// Resumes from inside `finished` itself. A queued resume could run after the dialog, and the
// window owning it, are gone; the coroutine must not outlive the emission.
class DialogFinished {
public:
    explicit DialogFinished(QDialog* dialog)
        : connection_(QObject::connect(dialog, &QDialog::finished, dialog, [this](int code) {
              code_ = code;
              finished_ = true;
              if (waiter_) std::exchange(waiter_, nullptr).resume();
          })) {}
    DialogFinished(const DialogFinished&) = delete;
    DialogFinished& operator=(const DialogFinished&) = delete;
    ~DialogFinished() { QObject::disconnect(connection_); }

    bool await_ready() const noexcept { return finished_; }
    void await_suspend(std::coroutine_handle<> waiting) noexcept { waiter_ = waiting; }
    int await_resume() const noexcept { return code_; }

private:
    QMetaObject::Connection connection_;
    std::coroutine_handle<> waiter_;
    int code_ = QDialog::Rejected;
    bool finished_ = false;
};

template <std::derived_from<QDialog> T, class... Args>
QCoro::Task<std::pair<int, T*>> awaitDialog(Args&&... args) {
    auto* dialog = new T(std::forward<Args>(args)...);
    installDialogChrome(dialog);
    dialog->setModal(true);

    // Subscribe before show(), so a finish that happens immediately is not missed.
    DialogFinished finished(dialog);
    dialog->show();
    const int code = co_await finished;

    dialog->deleteLater();
    co_return std::pair<int, T*>{code, dialog};
}

}  // namespace px::modrim
