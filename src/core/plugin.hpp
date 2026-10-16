#pragma once

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <QCoro/QCoroTask>
#include <QSet>
#include <QString>
#include <QStringList>
#include <spdlog/spdlog.h>

#include "core/context_fwd.hpp"
#include "core/error.hpp"
#include "core/toposort.hpp"

namespace px::modrim {

// `setup` runs synchronously in dependency order; `init` is awaited right after
// setup, for I/O or async start-up. `shutdown` runs in reverse order. Templated over
// the context type so the UI layer can register plugins against its own context.
template <class Ctx>
class BasicPlugin {
public:
    BasicPlugin(QString name, QStringList dependencies = {})
        : m_name(std::move(name)), m_dependencies(std::move(dependencies)) {}
    virtual ~BasicPlugin() = default;

    BasicPlugin(const BasicPlugin&) = delete;
    BasicPlugin& operator=(const BasicPlugin&) = delete;

    const QString& name() const noexcept { return m_name; }
    const QStringList& dependencies() const noexcept { return m_dependencies; }

    virtual void setup(Ctx&) {}
    virtual QCoro::Task<void> init(Ctx&) { co_return; }
    virtual QCoro::Task<void> shutdown() { co_return; }

private:
    QString m_name;
    QStringList m_dependencies;
};

template <class Ctx>
class BasicPluginRegistry {
public:
    using Plugin = BasicPlugin<Ctx>;

    // Replaces a plugin registered under the same name, keeping its position.
    void registerPlugin(std::unique_ptr<Plugin> plugin) {
        spdlog::debug("plugin registered: {}", plugin->name().toStdString());
        for (auto& existing : m_plugins) {
            if (existing->name() == plugin->name()) {
                existing = std::move(plugin);
                return;
            }
        }
        m_plugins.push_back(std::move(plugin));
    }

    Plugin* get(const QString& name) const {
        for (const auto& plugin : m_plugins) {
            if (plugin->name() == name) {
                return plugin.get();
            }
        }
        return nullptr;
    }

    QSet<QString> names() const {
        QSet<QString> result;
        for (const auto& plugin : m_plugins) {
            result.insert(plugin->name());
        }
        return result;
    }

    // `extra_deps` are dependencies satisfied outside the registry.
    Result<void> setupAll(Ctx& ctx, const QSet<QString>& extraDeps = {}) {
        for (const auto& plugin : m_plugins) {
            for (const auto& dep : plugin->dependencies()) {
                if (!get(dep) && !extraDeps.contains(dep)) {
                    return std::unexpected(Error{QStringLiteral("Plugin '%1' depends on '%2' which is not registered")
                                                     .arg(plugin->name(), dep)});
                }
            }
        }

        spdlog::info("setting up {} plugins...", m_plugins.size());
        auto order = dependencyOrder();
        if (!order) {
            return std::unexpected(order.error());
        }
        for (Plugin* plugin : *order) {
            plugin->setup(ctx);
        }
        spdlog::info("all plugins set up");
        return {};
    }

    QCoro::Task<Result<void>> initAll(Ctx& ctx) {
        spdlog::info("initializing {} plugins...", m_plugins.size());
        auto order = dependencyOrder();
        if (!order) {
            co_return std::unexpected(order.error());
        }
        for (Plugin* plugin : *order) {
            co_await plugin->init(ctx);
        }
        spdlog::info("all plugins initialized");
        co_return {};
    }

    QCoro::Task<Result<void>> shutdownAll() {
        spdlog::info("shutting down {} plugins...", m_plugins.size());
        auto order = dependencyOrder();
        if (!order) {
            co_return std::unexpected(order.error());
        }
        for (auto it = order->rbegin(); it != order->rend(); ++it) {
            co_await (*it)->shutdown();
        }
        spdlog::info("all plugins shut down");
        co_return {};
    }

private:
    Result<std::vector<Plugin*>> dependencyOrder() const {
        std::map<QString, QSet<QString>> graph;
        for (const auto& plugin : m_plugins) {
            const auto& deps = plugin->dependencies();
            graph.emplace(plugin->name(), QSet<QString>(deps.cbegin(), deps.cend()));
        }

        auto levels = toposortLevels(graph);
        if (!levels) {
            return std::unexpected(levels.error());
        }
        std::vector<Plugin*> order;
        order.reserve(m_plugins.size());
        for (const auto& level : *levels) {
            for (const auto& name : level) {
                order.push_back(get(name));
            }
        }
        return order;
    }

    std::vector<std::unique_ptr<Plugin>> m_plugins;
};

using Plugin = BasicPlugin<CoreContext>;
using PluginRegistry = BasicPluginRegistry<CoreContext>;

}  // namespace px::modrim
