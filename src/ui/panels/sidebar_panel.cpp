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
  const bool reset = entries.size() != m_entries.size();
  m_entries = entries;
  m_model->updateEntries(entries);
  if (reset)
    if (const auto current = currentEntry()) emit entrySelected(*current);
}
std::optional<view::SidebarEntry> SidebarPanel::currentEntry() const {
  const int selected = currentIndex();
  if (selected >= 0 && selected < m_entries.size()) return m_entries[selected];
  return std::nullopt;
}
void SidebarPanel::selectAllEntry() {
  auto* root = rootObject();
  auto* view =
      root ? root->findChild<QObject*>(QStringLiteral("listView")) : nullptr;
  if (!view) return;
  for (int row = 0; row < m_entries.size(); ++row)
    if (m_entries[row].kind == view::SidebarEntry::Kind::kAll) {
      view->setProperty("currentIndex", row);
      onActivated(row);
      return;
    }
}
void SidebarPanel::onActivated(int row) {
  if (row >= 0 && row < m_entries.size()) emit entrySelected(m_entries[row]);
}
}  // namespace px::modrim
