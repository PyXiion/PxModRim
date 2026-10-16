#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

#include <QCoro/QCoroTask>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/plugin.hpp"
#include "test_support.hpp"

// Stand-in for a composition root's context: records what each plugin did.
namespace {

struct TestContext {
    QStringList log;
};

using Plugin = px::modrim::BasicPlugin<TestContext>;
using PluginRegistry = px::modrim::BasicPluginRegistry<TestContext>;
using test_support::run_sync;

class RecorderPlugin : public Plugin {
public:
    RecorderPlugin(QString name, QStringList deps = {}, QStringList* shutdown_log = nullptr)
        : Plugin(std::move(name), std::move(deps)), shutdown_log_(shutdown_log) {}

    void setup(TestContext& ctx) override { ctx.log.append(name()); }

    QCoro::Task<void> init(TestContext& ctx) override {
        ctx.log.append(name());
        co_return;
    }

    QCoro::Task<void> shutdown() override {
        if (shutdown_log_) {
            shutdown_log_->append(name());
        }
        co_return;
    }

private:
    QStringList* shutdown_log_;
};

}  // namespace

TEST_CASE("empty registry sets up nothing") {
    PluginRegistry reg;
    TestContext ctx;
    REQUIRE(reg.setup_all(ctx).has_value());
    REQUIRE(ctx.log.isEmpty());
}

TEST_CASE("plugins without dependencies set up in name order") {
    PluginRegistry reg;
    reg.register_plugin(std::make_unique<RecorderPlugin>("a"));
    reg.register_plugin(std::make_unique<RecorderPlugin>("b"));
    reg.register_plugin(std::make_unique<RecorderPlugin>("c"));
    TestContext ctx;
    REQUIRE(reg.setup_all(ctx).has_value());
    REQUIRE(ctx.log == QStringList{"a", "b", "c"});
}

TEST_CASE("linear dependency chain sets up in order") {
    PluginRegistry reg;
    reg.register_plugin(std::make_unique<RecorderPlugin>("a"));
    reg.register_plugin(std::make_unique<RecorderPlugin>("b", QStringList{"a"}));
    reg.register_plugin(std::make_unique<RecorderPlugin>("c", QStringList{"b"}));
    TestContext ctx;
    REQUIRE(reg.setup_all(ctx).has_value());
    REQUIRE(ctx.log == QStringList{"a", "b", "c"});
}

TEST_CASE("complex DAG sets up level by level, names sorted within a level") {
    PluginRegistry reg;
    reg.register_plugin(std::make_unique<RecorderPlugin>("a"));
    reg.register_plugin(std::make_unique<RecorderPlugin>("b", QStringList{"a"}));
    reg.register_plugin(std::make_unique<RecorderPlugin>("c", QStringList{"a"}));
    reg.register_plugin(std::make_unique<RecorderPlugin>("d", QStringList{"b", "c"}));
    reg.register_plugin(std::make_unique<RecorderPlugin>("e", QStringList{"d"}));
    reg.register_plugin(std::make_unique<RecorderPlugin>("f"));
    reg.register_plugin(std::make_unique<RecorderPlugin>("g", QStringList{"e", "f"}));
    TestContext ctx;
    REQUIRE(reg.setup_all(ctx).has_value());
    REQUIRE(ctx.log == QStringList{"a", "f", "b", "c", "d", "e", "g"});
}

TEST_CASE("missing dependency is reported before anything is set up") {
    PluginRegistry reg;
    reg.register_plugin(std::make_unique<RecorderPlugin>("a", QStringList{"nonexistent"}));
    TestContext ctx;
    const auto result = reg.setup_all(ctx);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message.contains("depends on 'nonexistent'"));
    REQUIRE(ctx.log.isEmpty());
}

TEST_CASE("extra dependencies satisfy missing plugins") {
    PluginRegistry reg;
    reg.register_plugin(std::make_unique<RecorderPlugin>("a", QStringList{"external_dep"}));
    TestContext ctx;
    REQUIRE(reg.setup_all(ctx, QSet<QString>{"external_dep"}).has_value());
    REQUIRE(ctx.log == QStringList{"a"});
}

TEST_CASE("cyclic dependencies are reported as an error") {
    PluginRegistry reg;
    reg.register_plugin(std::make_unique<RecorderPlugin>("a", QStringList{"b"}));
    reg.register_plugin(std::make_unique<RecorderPlugin>("b", QStringList{"a"}));
    TestContext ctx;
    const auto result = reg.setup_all(ctx);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message.contains("Circular dependencies"));
    REQUIRE(ctx.log.isEmpty());
}

TEST_CASE("init runs in dependency order") {
    PluginRegistry reg;
    reg.register_plugin(std::make_unique<RecorderPlugin>("a"));
    reg.register_plugin(std::make_unique<RecorderPlugin>("b", QStringList{"a"}));
    reg.register_plugin(std::make_unique<RecorderPlugin>("c", QStringList{"b"}));
    TestContext ctx;
    REQUIRE(run_sync(reg.init_all(ctx)).has_value());
    REQUIRE(ctx.log == QStringList{"a", "b", "c"});
}

TEST_CASE("shutdown runs in reverse dependency order") {
    PluginRegistry reg;
    QStringList shutdown_order;
    reg.register_plugin(std::make_unique<RecorderPlugin>("dependent", QStringList{"dependency"}, &shutdown_order));
    reg.register_plugin(std::make_unique<RecorderPlugin>("dependency", QStringList{}, &shutdown_order));
    REQUIRE(run_sync(reg.shutdown_all()).has_value());
    REQUIRE(shutdown_order == QStringList{"dependent", "dependency"});
}
