#pragma once
#include "ui/components/filter_sidebar.hpp"
#include "ui/models/sidebar_model.hpp"
#include <cstdint>
namespace px::modrim { class CoreContext; }
namespace px::modrim {
class SidebarPanel : public FilterSidebar {
    Q_OBJECT
public:
    SidebarPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent = nullptr);
    ~SidebarPanel() override;
    void setEntries(const QList<view::SidebarEntry>& entries);
    std::optional<view::SidebarEntry> currentEntry() const;
    void selectAllEntry();
    SidebarModel* model() const { return m_model; }
signals:
    void entrySelected(const px::modrim::view::SidebarEntry& entry);
private:
    void onActivated(int row);
    CoreContext& m_ctx;
    SidebarModel* m_model;
    QList<view::SidebarEntry> m_entries;
    std::uint64_t m_sidebarConnection;
};
}
