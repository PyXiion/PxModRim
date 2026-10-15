#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QCoro/QCoroTask>
#include <QLatin1StringView>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "core/context.h"
#include "core/error.h"
#include "core/plugin.h"
#include "ui/settings_section.h"
#include "ui/ui_prefs.h"

class QQmlEngine;

namespace px::ui {

class AppContext;

// UI-layer plugins: same lifecycle as core plugins, but their context is the AppContext.
using UiPlugin = core::BasicPlugin<AppContext>;
using UiPluginRegistry = core::BasicPluginRegistry<AppContext>;

// Toast sink. Durations are milliseconds; the defaults match the legacy toast manager.
class Notifier {
public:
    virtual ~Notifier() = default;

    virtual void info(const QString& message, int durationMs = 3000) = 0;
    virtual void success(const QString& message, int durationMs = 3000) = 0;
    virtual void warning(const QString& message, int durationMs = 4000) = 0;
    virtual void error(const QString& message, int durationMs = 5000) = 0;
};

// Class-level metadata of a rail view, read before the view is constructed. Each
// RailView subclass declares `static constexpr RailViewMeta kRailMeta`.
struct RailViewMeta {
    QLatin1StringView viewId;
    QLatin1StringView iconName;
    QLatin1StringView label;
    // The download queue: the header's downloads button opens it, and the header
    // hides its own download progress while it is shown.
    bool listsDownloads = false;
};

// Base of widgets registered on the navigation rail.
class RailView : public QWidget {
    Q_OBJECT

public:
    RailView(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app);

    // Handles the part of a `modrim://<viewId>/…` link after the view id.
    virtual void openRoute(const QStringList& path);

    // Called when the rail reveals the view ahead of its first use, and when the window
    // closes. Views that hold resources (e.g. web profiles) release them in teardown().
    virtual void preload();
    virtual void teardown();

protected:
    core::CoreContext& ctx_;
    QQmlEngine* engine_;
    AppContext& app_;
};

using RailViewFactory = std::function<RailView*(core::CoreContext&, QQmlEngine*, QWidget*, AppContext&)>;

struct RailViewSpec {
    RailViewMeta meta;
    RailViewFactory create;
};

template <class V>
concept RailViewType = std::derived_from<V, RailView> && requires {
    V::kRailMeta;
};

// The UI composition context: owns the UI plugin registry, the rail and settings
// contributions, the toast sink and the navigator. Core state is read through
// coreContext(), which must outlive this object.
class AppContext {
public:
    AppContext(core::CoreContext& core, UiPrefs prefs = {});
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;
    ~AppContext();

    // ── Plugin system (UI layer) ──────────────────────
    void registerPlugin(std::unique_ptr<UiPlugin> plugin);
    UiPluginRegistry& plugins() noexcept { return plugins_; }

    // ── Rail views ────────────────────────────────────
    // Appends, or inserts at `position` when given.
    template <RailViewType V>
    void addRailView(std::optional<std::size_t> position = std::nullopt);
    const std::vector<RailViewSpec>& railViews() const noexcept { return rail_views_; }

    // ── Settings sections ─────────────────────────────
    // Each section is built per dialog, parented to that dialog.
    void addSettingsSection(SettingsSectionFactory factory);
    const std::vector<SettingsSectionFactory>& settingsSections() const noexcept { return settings_sections_; }

    // ── Toasts ────────────────────────────────────────
    // Not owned; nullptr when no toast surface is shown.
    Notifier* toasts() const noexcept { return toasts_; }
    void setToasts(Notifier* notifier) noexcept { toasts_ = notifier; }

    // ── Navigation ────────────────────────────────────
    // Opens a `modrim://<viewId>[/…]` link; a no-op until a navigator is set.
    void navigate(const QString& url) const;
    void setNavigator(std::function<void(const QString&)> navigate);

    // ── UI prefs ──────────────────────────────────────
    UiPrefs& uiPrefs() noexcept { return ui_prefs_; }
    const UiPrefs& uiPrefs() const noexcept { return ui_prefs_; }

    // ── Core access ───────────────────────────────────
    core::CoreContext& coreContext() const noexcept { return core_; }

    // ── Lifecycle ─────────────────────────────────────
    // Core plugins first, then UI plugins; UI plugins may depend on core plugin names.
    core::Result<void> setupAll();
    QCoro::Task<core::Result<void>> initAll();
    // Reloads mods and resolves to how many are now loaded.
    QCoro::Task<core::Result<int>> refreshMods(bool full = false);
    // UI plugins shut down before core plugins.
    QCoro::Task<core::Result<void>> shutdownAll();

private:
    core::CoreContext& core_;
    UiPluginRegistry plugins_;
    std::vector<RailViewSpec> rail_views_;
    std::vector<SettingsSectionFactory> settings_sections_;
    Notifier* toasts_ = nullptr;
    std::function<void(const QString&)> navigate_;
    UiPrefs ui_prefs_;
};

template <RailViewType V>
void AppContext::addRailView(std::optional<std::size_t> position) {
    RailViewSpec spec{
        V::kRailMeta,
        [](core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app) -> RailView* {
            return new V(ctx, engine, parent, app);
        },
    };
    if (position) {
        rail_views_.insert(rail_views_.begin() + static_cast<std::ptrdiff_t>(*position), std::move(spec));
    } else {
        rail_views_.push_back(std::move(spec));
    }
}

}  // namespace px::ui
