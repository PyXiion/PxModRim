#include "core/loading.h"

#include <algorithm>

namespace px::modrim {

LoadingState::LoadingState(QObject* parent) : QObject(parent) {}

QStringList LoadingState::statuses() const {
    QStringList result;
    result.reserve(static_cast<qsizetype>(stack_.size()));
    for (const auto& frame : stack_) {
        result.append(frame.status);
    }
    return result;
}

void LoadingState::push(const QString& status, int total_steps) {
    stack_.push_back(Frame{.status = status, .current_step = 0, .total_steps = total_steps});
    emit_changed();
}

void LoadingState::pop() {
    if (!stack_.empty()) {
        stack_.pop_back();
    }
    if (stack_.empty()) {
        Q_EMIT finished();
    } else {
        emit_changed();
    }
}

void LoadingState::step(int amount) {
    if (stack_.empty()) {
        return;
    }
    auto& frame = stack_.back();
    frame.current_step = std::min(frame.total_steps, frame.current_step + amount);
    emit_changed();
}

void LoadingState::set_total(int total) {
    if (stack_.empty()) {
        return;
    }
    stack_.back().total_steps = std::max(1, total);
    emit_changed();
}

void LoadingState::set_progress(int value) {
    if (stack_.empty()) {
        return;
    }
    auto& frame = stack_.back();
    frame.current_step = std::max(0, std::min(frame.total_steps, value));
    emit_changed();
}

LoadingState::TaskScope LoadingState::task(const QString& status, int total_steps) {
    push(status, total_steps);
    return TaskScope(*this);
}

void LoadingState::emit_changed() {
    if (stack_.empty()) {
        return;
    }
    const auto& frame = stack_.back();
    // Truncated through double, as the legacy float division does: 29/100 * 100
    // is 28.999... and must still report 28.
    const int pct = frame.total_steps != 0
                        ? static_cast<int>((static_cast<double>(frame.current_step) /
                                            static_cast<double>(frame.total_steps)) *
                                           100.0)
                        : 0;
    Q_EMIT changed(frame.status, pct, frame.current_step, frame.total_steps);
}

}  // namespace px::modrim
