#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

namespace px::modrim {

// Stack of progress frames for one async operation. Each frame is a subtask;
// signals describe the innermost (top) frame.
class LoadingState : public QObject {
  Q_OBJECT

 public:
  // Pops its frame on destruction.
  class TaskScope {
   public:
    explicit TaskScope(LoadingState& state) noexcept : m_state(&state) {}
    TaskScope(TaskScope&& other) noexcept
        : m_state(std::exchange(other.m_state, nullptr)) {}
    TaskScope(const TaskScope&) = delete;
    TaskScope& operator=(const TaskScope&) = delete;
    TaskScope& operator=(TaskScope&&) = delete;
    ~TaskScope() {
      if (m_state) {
        m_state->pop();
      }
    }

   private:
    LoadingState* m_state;
  };

  explicit LoadingState(QObject* parent = nullptr);

  QStringList statuses() const;

  void push(const QString& status, int totalSteps = 100);
  void pop();
  void step(int amount = 1);
  void setTotal(int total);
  void setProgress(int value);

  [[nodiscard]] TaskScope task(const QString& status, int totalSteps = 100);

 Q_SIGNALS:
  void changed(const QString& status, int pct, int current, int total);
  void finished();

 private:
  struct Frame {
    QString status;
    int currentStep = 0;
    int totalSteps = 100;
  };

  void emitChanged();

  std::vector<Frame> m_stack;
};

}  // namespace px::modrim
