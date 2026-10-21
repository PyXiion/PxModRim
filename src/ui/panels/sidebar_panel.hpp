#pragma once
#include <cstdint>

#include "ui/components/filter_sidebar.hpp"
#include "ui/models/sidebar_model.hpp"
namespace px::modrim {
class CoreContext;
}
namespace px::modrim {
class SidebarPanel : public FilterSidebar {
  Q_OBJECT
 public:
  SidebarPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent = nullptr);
  ~SidebarPanel() override;
  std::optional<view::SidebarEntry> currentEntry() const;
  void selectAllEntry();
 signals:
  void entrySelected(const px::modrim::view::SidebarEntry& entry);

 private:
  void setEntries(const QList<view::SidebarEntry>& entries);
  void onActivated(int row);
  CoreContext& m_ctx;
  SidebarModel* m_model;
  std::uint64_t m_sidebarConnection;
};
}  // namespace px::modrim
