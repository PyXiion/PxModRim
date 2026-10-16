#pragma once

#ifdef PX_HAS_STEAM

#include "ui/plugins/steam_workshop/plugin.h"
#include "ui/views/base.h"

class QQuickWidget;
class QShowEvent;

namespace px::modrim {

class DownloadSidebar;

class SteamWorkshopViewPanel final : public BaseViewPanel {
    Q_OBJECT
public:
    static constexpr RailViewMeta kRailMeta{QLatin1StringView("steam_workshop"), QLatin1StringView("steam"), QLatin1StringView("Steam Workshop")};
    SteamWorkshopViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app);
    ~SteamWorkshopViewPanel() override;
    void preload();
    void teardown();
    Q_INVOKABLE void navigateHome();
    Q_INVOKABLE void navigateToUrl(const QString& text);
protected:
    void showEvent(QShowEvent* event) override;
private:
    void ensure_initialized();
    QObject* web() const;
    void run_js(const QString& code);
    DownloadSidebar* sidebar_;
    QQuickWidget* qml_;
    SteamWorkshopUiPlugin* plugin_ = nullptr;
    bool initialized_ = false;
    Event<QStringList>::Id badges_id_ = 0;
    Event<QStringList>::Id active_id_ = 0;
    Event<SidebarSync>::Id sidebar_id_ = 0;
    Event<DownloadItemStatus>::Id status_id_ = 0;
    Event<bool>::Id busy_id_ = 0;
    Event<QString>::Id uncheck_id_ = 0;
    Event<std::monostate>::Id clear_id_ = 0;
};

}  // namespace px::modrim
#endif
