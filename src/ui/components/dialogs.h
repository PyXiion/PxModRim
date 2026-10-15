#pragma once

// Modal dialogs as coroutines: `co_await awaitDialog<T>(args...)` shows the dialog and
// resumes with its result code and pointer. The dialog is scheduled for deletion, so
// read what you need from it before the next event-loop pass (the coroutine resumes
// synchronously from `finished`, so that holds for any code after the co_await).

#include <concepts>
#include <utility>

#include <QCoro/QCoroSignal>
#include <QCoro/QCoroTask>
#include <QDialog>

#include "ui/components/dialog_chrome.h"

namespace px::ui {

template <std::derived_from<QDialog> T, class... Args>
QCoro::Task<std::pair<int, T*>> awaitDialog(Args&&... args) {
    auto* dialog = new T(std::forward<Args>(args)...);
    installDialogChrome(dialog);
    dialog->setModal(true);

    // Subscribe before show(), so a finish that happens immediately is not missed.
    auto finished = qCoro(dialog, &QDialog::finished);
    dialog->show();
    const int code = co_await std::move(finished);

    dialog->deleteLater();
    co_return std::pair<int, T*>{code, dialog};
}

}  // namespace px::ui
