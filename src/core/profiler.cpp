#include "core/profiler.h"

#include <spdlog/spdlog.h>

namespace px::core {

namespace {

void renderNode(const Profiler::Node& node, int depth, QString& out) {
    using namespace std::chrono;
    const double ms = duration<double, std::milli>(node.elapsed).count();
    out += QString(depth * 2, QChar(' ')) + node.name + QStringLiteral(": ") +
           QString::number(ms, 'f', 3) + QStringLiteral(" ms\n");
    for (const auto& child : node.children) {
        renderNode(*child, depth + 1, out);
    }
}

}  // namespace

Profiler::Scope::Scope(Profiler& owner, QAnyStringView name)
    : owner_(owner), parent_(owner.current_), start_(std::chrono::steady_clock::now()) {
    auto node = std::make_unique<Node>(name.toString());
    node_ = node.get();
    parent_->children.push_back(std::move(node));
    owner_.current_ = node_;
}

Profiler::Scope::~Scope() {
    node_->elapsed = std::chrono::steady_clock::now() - start_;
    owner_.current_ = parent_;
}

void Profiler::adopt(Profiler& child) {
    for (auto& node : child.root_.children) {
        current_->children.push_back(std::move(node));
    }
    child.root_.children.clear();
}

QString Profiler::render() const {
    QString out;
    for (const auto& child : root_.children) {
        renderNode(*child, 0, out);
    }
    return out;
}

ProfileSession::ProfileSession(QAnyStringView label)
    : scope_(std::make_unique<Profiler::Scope>(timer_, label)) {}

ProfileSession::~ProfileSession() {
    // Close the top-level scope before rendering so its elapsed time is included.
    scope_.reset();
    spdlog::debug("Profile report:\n{}", timer_.render().toStdString());
}

}  // namespace px::core
