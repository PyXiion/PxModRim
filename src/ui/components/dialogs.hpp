#pragma once

// Modal dialogs as coroutines: `co_await awaitDialog<T>(args...)` shows the
// dialog and resumes with its result code and pointer. The dialog is scheduled
// for deletion, so read what you need from it before the next event-loop pass
// (the coroutine resumes synchronously from `finished`, so that holds for any
// code after the co_await).

#include <QDialog>
#include <QObject>
#include <concepts>
#include <coroutine>
#include <utility>

#include <QCoro/QCoroTask>

#include "ui/components/dialog_chrome.hpp"

namespace px::modrim {

// Resumes from inside `finished` itself. A queued resume could run after the
// dialog, and the window owning it, are gone; the coroutine must not outlive
// the emission.
class DialogFinished {
 public:
  explicit DialogFinished(QDialog* dialog)
      : m_connection(QObject::connect(
            dialog, &QDialog::finished, dialog, [this](int code) {
              m_code = code;
              m_finished = true;
              if (m_waiter) std::exchange(m_waiter, nullptr).resume();
            })) {}
  DialogFinished(const DialogFinished&) = delete;
  DialogFinished& operator=(const DialogFinished&) = delete;
  ~DialogFinished() { QObject::disconnect(m_connection); }

  // NOLINTBEGIN(readability-identifier-naming): coroutine awaiter protocol
  bool await_ready() const noexcept { return m_finished; }
  void await_suspend(std::coroutine_handle<> waiting) noexcept {
    m_waiter = waiting;
  }
  int await_resume() const noexcept { return m_code; }
  // NOLINTEND(readability-identifier-naming)

 private:
  QMetaObject::Connection m_connection;
  std::coroutine_handle<> m_waiter;
  int m_code = QDialog::Rejected;
  bool m_finished = false;
};

template <std::derived_from<QDialog> T, class... Args>
QCoro::Task<std::pair<int, T*>> awaitDialog(Args&&... args) {
  auto* dialog = new T(std::forward<Args>(args)...);
  installDialogChrome(dialog);
  dialog->setModal(true);

  // Subscribe before show(), so a finish that happens immediately is not
  // missed.
  DialogFinished finished(dialog);
  dialog->show();
  const int code = co_await finished;

  dialog->deleteLater();
  co_return std::pair<int, T*>{code, dialog};
}

}  // namespace px::modrim
