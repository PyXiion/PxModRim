#include "ui/panels/sidebar_panel.hpp"

#include <QQuickItem>

#include "core/context.hpp"
#include "core/services/diagnostics_service.hpp"
namespace px::modrim {
SidebarPanel::SidebarPanel(CoreContext& ctx, QQmlEngine* engine,
                           QWidget* parent)
    : FilterSidebar(engine, nullptr, parent),
      m_ctx(ctx),
      m_model(new SidebarModel(this)) {
  if (rootObject())
    rootObject()->setProperty("model", QVariant::fromValue(m_model));
  connect(this, &FilterSidebar::activated, this, &SidebarPanel::onActivated);
  m_sidebarConnection =
      m_ctx.diagnosticsService().sidebarEntriesChanged.connect(
          [this](const auto& entries) { setEntries(entries); });
}
SidebarPanel::~SidebarPanel() {
  m_ctx.diagnosticsService().sidebarEntriesChanged.disconnect(
      m_sidebarConnection);
}
void SidebarPanel::setEntries(const QList<view::SidebarEntry>& entries) {
  const bool reset = entries.size() != m_model->rowCount();
  m_model->updateEntries(entries);
  if (reset)
    if (const auto current = currentEntry()) emit entrySelected(*current);
}
std::optional<view::SidebarEntry> SidebarPanel::currentEntry() const {
  return m_model->getEntry(currentIndex());
}
void SidebarPanel::selectAllEntry() {
  auto* root = rootObject();
  auto* view =
      root ? root->findChild<QObject*>(QStringLiteral("listView")) : nullptr;
  if (!view) return;
  for (int row = 0; row < m_model->rowCount(); ++row)
    if (m_model->getEntry(row)->kind == view::SidebarEntry::Kind::kAll) {
      view->setProperty("currentIndex", row);
      onActivated(row);
      return;
    }
}
void SidebarPanel::onActivated(int row) {
  if (const auto entry = m_model->getEntry(row)) emit entrySelected(*entry);
}
}  // namespace px::modrim
