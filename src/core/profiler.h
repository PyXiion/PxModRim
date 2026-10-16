#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include <QAnyStringView>
#include <QString>

namespace px::modrim {

// Hierarchical wall-clock timer. Scopes nest according to their lifetimes; `render()` prints
// the resulting tree.
class Profiler {
public:
    struct Node {
        explicit Node(QString nodeName) : name(std::move(nodeName)) {}

        QString name;
        std::chrono::steady_clock::duration elapsed{};
        std::vector<std::unique_ptr<Node>> children;
    };

    class Scope {
    public:
        Scope(Profiler& owner, QAnyStringView name);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;

    private:
        Profiler& owner_;
        Node* node_;
        Node* parent_;
        std::chrono::steady_clock::time_point start_;
    };

    Profiler() = default;

    [[nodiscard]] Scope operator()(QAnyStringView name) { return Scope(*this, name); }

    // Moves the root timings of `child` under the currently open scope of this profiler, so a
    // sub-profile collected elsewhere shows up in this report. `child` is left empty.
    void adopt(Profiler& child);

    [[nodiscard]] QString render() const;

private:
    Node root_{QStringLiteral("<root>")};
    Node* current_ = &root_;
};

// Logs the hierarchical report of a block at debug level when it ends.
class ProfileSession {
public:
    explicit ProfileSession(QAnyStringView label);
    ~ProfileSession();
    ProfileSession(const ProfileSession&) = delete;
    ProfileSession& operator=(const ProfileSession&) = delete;

    [[nodiscard]] Profiler& timer() { return timer_; }

private:
    Profiler timer_;
    std::unique_ptr<Profiler::Scope> scope_;
};

}  // namespace px::modrim
