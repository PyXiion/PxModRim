#include "core/loading.hpp"

#include <algorithm>

namespace px::modrim {

LoadingState::LoadingState(QObject* parent) : QObject(parent) {}

QStringList LoadingState::statuses() const {
    QStringList result;
    result.reserve(static_cast<qsizetype>(m_stack.size()));
    for (const auto& frame : m_stack) {
        result.append(frame.status);
    }
    return result;
}

void LoadingState::push(const QString& status, int totalSteps) {
    m_stack.push_back(Frame{.status = status, .currentStep = 0, .totalSteps = totalSteps});
    emitChanged();
}

void LoadingState::pop() {
    if (!m_stack.empty()) {
        m_stack.pop_back();
    }
    if (m_stack.empty()) {
        Q_EMIT finished();
    } else {
        emitChanged();
    }
}

void LoadingState::step(int amount) {
    if (m_stack.empty()) {
        return;
    }
    auto& frame = m_stack.back();
    frame.currentStep = std::min(frame.totalSteps, frame.currentStep + amount);
    emitChanged();
}

void LoadingState::setTotal(int total) {
    if (m_stack.empty()) {
        return;
    }
    m_stack.back().totalSteps = std::max(1, total);
    emitChanged();
}

void LoadingState::setProgress(int value) {
    if (m_stack.empty()) {
        return;
    }
    auto& frame = m_stack.back();
    frame.currentStep = std::max(0, std::min(frame.totalSteps, value));
    emitChanged();
}

LoadingState::TaskScope LoadingState::task(const QString& status, int totalSteps) {
    push(status, totalSteps);
    return TaskScope(*this);
}

void LoadingState::emitChanged() {
    if (m_stack.empty()) {
        return;
    }
    const auto& frame = m_stack.back();
    // Truncated through double, as the legacy float division does: 29/100 * 100
    // is 28.999... and must still report 28.
    const int pct = frame.totalSteps != 0
                        ? static_cast<int>((static_cast<double>(frame.currentStep) /
                                            static_cast<double>(frame.totalSteps)) *
                                           100.0)
                        : 0;
    Q_EMIT changed(frame.status, pct, frame.currentStep, frame.totalSteps);
}

}  // namespace px::modrim
