#include "ui/context.hpp"

#include <expected>
#include <utility>

#include "core/mod_service.hpp"

namespace px::modrim {

RailView::RailView(CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
                   AppContext& app)
    : QWidget(parent), m_ctx(ctx), m_engine(engine), m_app(app) {}

void RailView::openRoute(const QStringList&) {}

void RailView::preload() {}

void RailView::teardown() {}

AppContext::AppContext(CoreContext& core, UiPrefs prefs,
                       ResolvedScale initialScale)
    : m_core(core),
      m_uiPrefs(std::move(prefs)),
      m_initialScale(std::move(initialScale)) {}

AppContext::~AppContext() = default;

void AppContext::registerPlugin(std::unique_ptr<UiPlugin> plugin) {
  m_plugins.registerPlugin(std::move(plugin));
}

void AppContext::addSettingsSection(SettingsSectionFactory factory) {
  m_settingsSections.push_back(std::move(factory));
}

void AppContext::navigate(const QString& url) const {
  if (m_navigate) {
    m_navigate(url);
  }
}

void AppContext::setNavigator(std::function<void(const QString&)> navigate) {
  m_navigate = std::move(navigate);
}

Result<void> AppContext::setupAll() {
  if (auto core = m_core.plugins().setupAll(m_core); !core) {
    return std::unexpected(core.error());
  }
  return m_plugins.setupAll(*this, m_core.plugins().names());
}

QCoro::Task<Result<void>> AppContext::initAll() {
  if (m_pluginsInitialized) {
    co_return {};
  }
  if (auto core = co_await m_core.plugins().initAll(m_core); !core) {
    co_return std::unexpected(core.error());
  }
  if (auto ui = co_await m_plugins.initAll(*this); !ui) {
    co_return std::unexpected(ui.error());
  }
  m_pluginsInitialized = true;
  co_return {};
}

QCoro::Task<Result<int>> AppContext::refreshMods(bool full) {
  if (auto reloaded = co_await m_core.modService().reload(full); !reloaded) {
    co_return std::unexpected(reloaded.error());
  }
  co_return static_cast<int>(m_core.allMods().size());
}

QCoro::Task<Result<void>> AppContext::shutdownAll() {
  if (auto ui = co_await m_plugins.shutdownAll(); !ui) {
    co_return std::unexpected(ui.error());
  }
  co_return co_await m_core.plugins().shutdownAll();
}

}  // namespace px::modrim
