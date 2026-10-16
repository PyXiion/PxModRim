#include "ui/context.h"

#include <expected>
#include <utility>

#include "core/mod_service.h"

namespace px::modrim {

RailView::RailView(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app)
    : QWidget(parent), ctx_(ctx), engine_(engine), app_(app) {}

void RailView::openRoute(const QStringList&) {}

void RailView::preload() {}

void RailView::teardown() {}

AppContext::AppContext(CoreContext& core, UiPrefs prefs)
    : core_(core), ui_prefs_(std::move(prefs)) {}

AppContext::~AppContext() = default;

void AppContext::registerPlugin(std::unique_ptr<UiPlugin> plugin) {
    plugins_.register_plugin(std::move(plugin));
}

void AppContext::addSettingsSection(SettingsSectionFactory factory) {
    settings_sections_.push_back(std::move(factory));
}

void AppContext::navigate(const QString& url) const {
    if (navigate_) {
        navigate_(url);
    }
}

void AppContext::setNavigator(std::function<void(const QString&)> navigate) {
    navigate_ = std::move(navigate);
}

Result<void> AppContext::setupAll() {
    if (auto core = core_.plugins().setup_all(core_); !core) {
        return std::unexpected(core.error());
    }
    return plugins_.setup_all(*this, core_.plugins().names());
}

QCoro::Task<Result<void>> AppContext::initAll() {
    if (auto core = co_await core_.plugins().init_all(core_); !core) {
        co_return std::unexpected(core.error());
    }
    co_return co_await plugins_.init_all(*this);
}

QCoro::Task<Result<int>> AppContext::refreshMods(bool full) {
    if (auto reloaded = co_await core_.modService().reload(full); !reloaded) {
        co_return std::unexpected(reloaded.error());
    }
    co_return static_cast<int>(core_.allMods().size());
}

QCoro::Task<Result<void>> AppContext::shutdownAll() {
    if (auto ui = co_await plugins_.shutdown_all(); !ui) {
        co_return std::unexpected(ui.error());
    }
    co_return co_await core_.plugins().shutdown_all();
}

}  // namespace px::modrim
