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

#include "core/context_fwd.h"
#include "core/error.h"
#include "core/toposort.h"

namespace px::core {

// `setup` runs synchronously in dependency order; `init` is awaited right after
// setup, for I/O or async start-up. `shutdown` runs in reverse order. Templated over
// the context type so the UI layer can register plugins against its own context.
template <class Ctx>
class BasicPlugin {
public:
    BasicPlugin(QString name, QStringList dependencies = {})
        : name_(std::move(name)), dependencies_(std::move(dependencies)) {}
    virtual ~BasicPlugin() = default;

    BasicPlugin(const BasicPlugin&) = delete;
    BasicPlugin& operator=(const BasicPlugin&) = delete;

    const QString& name() const noexcept { return name_; }
    const QStringList& dependencies() const noexcept { return dependencies_; }

    virtual void setup(Ctx&) {}
    virtual QCoro::Task<void> init(Ctx&) { co_return; }
    virtual QCoro::Task<void> shutdown() { co_return; }

private:
    QString name_;
    QStringList dependencies_;
};

template <class Ctx>
class BasicPluginRegistry {
public:
    using Plugin = BasicPlugin<Ctx>;

    // Replaces a plugin registered under the same name, keeping its position.
    void register_plugin(std::unique_ptr<Plugin> plugin) {
        spdlog::debug("plugin registered: {}", plugin->name().toStdString());
        for (auto& existing : plugins_) {
            if (existing->name() == plugin->name()) {
                existing = std::move(plugin);
                return;
            }
        }
        plugins_.push_back(std::move(plugin));
    }

    Plugin* get(const QString& name) const {
        for (const auto& plugin : plugins_) {
            if (plugin->name() == name) {
                return plugin.get();
            }
        }
        return nullptr;
    }

    QSet<QString> names() const {
        QSet<QString> result;
        for (const auto& plugin : plugins_) {
            result.insert(plugin->name());
        }
        return result;
    }

    // `extra_deps` are dependencies satisfied outside the registry.
    Result<void> setup_all(Ctx& ctx, const QSet<QString>& extra_deps = {}) {
        for (const auto& plugin : plugins_) {
            for (const auto& dep : plugin->dependencies()) {
                if (!get(dep) && !extra_deps.contains(dep)) {
                    return std::unexpected(Error{QStringLiteral("Plugin '%1' depends on '%2' which is not registered")
                                                     .arg(plugin->name(), dep)});
                }
            }
        }

        spdlog::info("setting up {} plugins...", plugins_.size());
        auto order = dependency_order();
        if (!order) {
            return std::unexpected(order.error());
        }
        for (Plugin* plugin : *order) {
            plugin->setup(ctx);
        }
        spdlog::info("all plugins set up");
        return {};
    }

    QCoro::Task<Result<void>> init_all(Ctx& ctx) {
        spdlog::info("initializing {} plugins...", plugins_.size());
        auto order = dependency_order();
        if (!order) {
            co_return std::unexpected(order.error());
        }
        for (Plugin* plugin : *order) {
            co_await plugin->init(ctx);
        }
        spdlog::info("all plugins initialized");
        co_return {};
    }

    QCoro::Task<Result<void>> shutdown_all() {
        spdlog::info("shutting down {} plugins...", plugins_.size());
        auto order = dependency_order();
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
    Result<std::vector<Plugin*>> dependency_order() const {
        std::map<QString, QSet<QString>> graph;
        for (const auto& plugin : plugins_) {
            const auto& deps = plugin->dependencies();
            graph.emplace(plugin->name(), QSet<QString>(deps.cbegin(), deps.cend()));
        }

        auto levels = toposort_levels(graph);
        if (!levels) {
            return std::unexpected(levels.error());
        }
        std::vector<Plugin*> order;
        order.reserve(plugins_.size());
        for (const auto& level : *levels) {
            for (const auto& name : level) {
                order.push_back(get(name));
            }
        }
        return order;
    }

    std::vector<std::unique_ptr<Plugin>> plugins_;
};

using Plugin = BasicPlugin<CoreContext>;
using PluginRegistry = BasicPluginRegistry<CoreContext>;

}  // namespace px::core
