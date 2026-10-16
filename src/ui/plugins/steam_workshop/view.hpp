#pragma once

#ifdef PX_HAS_STEAM

#include "ui/plugins/steam_workshop/plugin.hpp"
#include "ui/views/base.hpp"

class QQuickWidget;
class QShowEvent;

namespace px::modrim {

class DownloadSidebar;

class SteamWorkshopViewPanel final : public BaseViewPanel {
  Q_OBJECT
 public:
  static constexpr RailViewMeta kRailMeta{QLatin1StringView("steam_workshop"),
                                          QLatin1StringView("steam"),
                                          QLatin1StringView("Steam Workshop")};
  SteamWorkshopViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
                         AppContext& app);
  ~SteamWorkshopViewPanel() override;
  void preload();
  void teardown();
  Q_INVOKABLE void navigateHome();
  Q_INVOKABLE void navigateToUrl(const QString& text);

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  void ensureInitialized();
  QObject* web() const;
  void runJs(const QString& code);
  DownloadSidebar* m_sidebar;
  QQuickWidget* m_qml;
  SteamWorkshopUiPlugin* m_plugin = nullptr;
  bool m_initialized = false;
  Event<QStringList>::Id m_badgesId = 0;
  Event<QStringList>::Id m_activeId = 0;
  Event<SidebarSync>::Id m_sidebarId = 0;
  Event<DownloadItemStatus>::Id m_statusId = 0;
  Event<bool>::Id m_busyId = 0;
  Event<QString>::Id m_uncheckId = 0;
  Event<std::monostate>::Id m_clearId = 0;
};

}  // namespace px::modrim
#endif
