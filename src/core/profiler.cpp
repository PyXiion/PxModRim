#include "core/profiler.hpp"

#include <spdlog/spdlog.h>

namespace px::modrim {

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
    : m_owner(owner),
      m_parent(owner.m_current),
      m_start(std::chrono::steady_clock::now()) {
  auto node = std::make_unique<Node>(name.toString());
  m_node = node.get();
  m_parent->children.push_back(std::move(node));
  m_owner.m_current = m_node;
}

Profiler::Scope::~Scope() {
  m_node->elapsed = std::chrono::steady_clock::now() - m_start;
  m_owner.m_current = m_parent;
}

void Profiler::adopt(Profiler& child) {
  for (auto& node : child.m_root.children) {
    m_current->children.push_back(std::move(node));
  }
  child.m_root.children.clear();
}

QString Profiler::render() const {
  QString out;
  for (const auto& child : m_root.children) {
    renderNode(*child, 0, out);
  }
  return out;
}

ProfileSession::ProfileSession(QAnyStringView label)
    : m_scope(std::make_unique<Profiler::Scope>(m_timer, label)) {}

ProfileSession::~ProfileSession() {
  // Close the top-level scope before rendering so its elapsed time is included.
  m_scope.reset();
  spdlog::debug("Profile report:\n{}", m_timer.render().toStdString());
}

}  // namespace px::modrim
