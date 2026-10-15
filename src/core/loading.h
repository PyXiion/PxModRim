#pragma once

#include <utility>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>

namespace px::core {

// Stack of progress frames for one async operation. Each frame is a subtask;
// signals describe the innermost (top) frame.
class LoadingState : public QObject {
    Q_OBJECT

public:
    // Pops its frame on destruction.
    class TaskScope {
    public:
        explicit TaskScope(LoadingState& state) noexcept : state_(&state) {}
        TaskScope(TaskScope&& other) noexcept : state_(std::exchange(other.state_, nullptr)) {}
        TaskScope(const TaskScope&) = delete;
        TaskScope& operator=(const TaskScope&) = delete;
        TaskScope& operator=(TaskScope&&) = delete;
        ~TaskScope() {
            if (state_) {
                state_->pop();
            }
        }

    private:
        LoadingState* state_;
    };

    explicit LoadingState(QObject* parent = nullptr);

    QStringList statuses() const;

    void push(const QString& status, int total_steps = 100);
    void pop();
    void step(int amount = 1);
    void set_total(int total);
    void set_progress(int value);

    [[nodiscard]] TaskScope task(const QString& status, int total_steps = 100);

Q_SIGNALS:
    void changed(const QString& status, int pct, int current, int total);
    void finished();

private:
    struct Frame {
        QString status;
        int current_step = 0;
        int total_steps = 100;
    };

    void emit_changed();

    std::vector<Frame> stack_;
};

}  // namespace px::core
