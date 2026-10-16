#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <utility>

#include <QCoro/QCoroTask>

#include <catch2/catch_test_macros.hpp>

#include "core/plugin.hpp"
#include "test_support.hpp"

// Stand-in for a composition root's context: records what each plugin did.
namespace {

struct TestContext {
  QStringList log;
};

using Plugin = px::modrim::BasicPlugin<TestContext>;
using PluginRegistry = px::modrim::BasicPluginRegistry<TestContext>;
using test_support::runSync;

class RecorderPlugin : public Plugin {
 public:
  RecorderPlugin(QString name, QStringList deps = {},
                 QStringList* shutdownLog = nullptr)
      : Plugin(std::move(name), std::move(deps)), m_shutdownLog(shutdownLog) {}

  void setup(TestContext& ctx) override { ctx.log.append(name()); }

  QCoro::Task<void> init(TestContext& ctx) override {
    ctx.log.append(name());
    co_return;
  }

  QCoro::Task<void> shutdown() override {
    if (m_shutdownLog) {
      m_shutdownLog->append(name());
    }
    co_return;
  }

 private:
  QStringList* m_shutdownLog;
};

}  // namespace

TEST_CASE("empty registry sets up nothing") {
  PluginRegistry reg;
  TestContext ctx;
  REQUIRE(reg.setupAll(ctx).has_value());
  REQUIRE(ctx.log.isEmpty());
}

TEST_CASE("plugins without dependencies set up in name order") {
  PluginRegistry reg;
  reg.registerPlugin(std::make_unique<RecorderPlugin>("a"));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("b"));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("c"));
  TestContext ctx;
  REQUIRE(reg.setupAll(ctx).has_value());
  REQUIRE(ctx.log == QStringList{"a", "b", "c"});
}

TEST_CASE("linear dependency chain sets up in order") {
  PluginRegistry reg;
  reg.registerPlugin(std::make_unique<RecorderPlugin>("a"));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("b", QStringList{"a"}));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("c", QStringList{"b"}));
  TestContext ctx;
  REQUIRE(reg.setupAll(ctx).has_value());
  REQUIRE(ctx.log == QStringList{"a", "b", "c"});
}

TEST_CASE("complex DAG sets up level by level, names sorted within a level") {
  PluginRegistry reg;
  reg.registerPlugin(std::make_unique<RecorderPlugin>("a"));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("b", QStringList{"a"}));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("c", QStringList{"a"}));
  reg.registerPlugin(
      std::make_unique<RecorderPlugin>("d", QStringList{"b", "c"}));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("e", QStringList{"d"}));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("f"));
  reg.registerPlugin(
      std::make_unique<RecorderPlugin>("g", QStringList{"e", "f"}));
  TestContext ctx;
  REQUIRE(reg.setupAll(ctx).has_value());
  REQUIRE(ctx.log == QStringList{"a", "f", "b", "c", "d", "e", "g"});
}

TEST_CASE("missing dependency is reported before anything is set up") {
  PluginRegistry reg;
  reg.registerPlugin(
      std::make_unique<RecorderPlugin>("a", QStringList{"nonexistent"}));
  TestContext ctx;
  const auto result = reg.setupAll(ctx);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().message.contains("depends on 'nonexistent'"));
  REQUIRE(ctx.log.isEmpty());
}

TEST_CASE("extra dependencies satisfy missing plugins") {
  PluginRegistry reg;
  reg.registerPlugin(
      std::make_unique<RecorderPlugin>("a", QStringList{"external_dep"}));
  TestContext ctx;
  REQUIRE(reg.setupAll(ctx, QSet<QString>{"external_dep"}).has_value());
  REQUIRE(ctx.log == QStringList{"a"});
}

TEST_CASE("cyclic dependencies are reported as an error") {
  PluginRegistry reg;
  reg.registerPlugin(std::make_unique<RecorderPlugin>("a", QStringList{"b"}));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("b", QStringList{"a"}));
  TestContext ctx;
  const auto result = reg.setupAll(ctx);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().message.contains("Circular dependencies"));
  REQUIRE(ctx.log.isEmpty());
}

TEST_CASE("init runs in dependency order") {
  PluginRegistry reg;
  reg.registerPlugin(std::make_unique<RecorderPlugin>("a"));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("b", QStringList{"a"}));
  reg.registerPlugin(std::make_unique<RecorderPlugin>("c", QStringList{"b"}));
  TestContext ctx;
  REQUIRE(runSync(reg.initAll(ctx)).has_value());
  REQUIRE(ctx.log == QStringList{"a", "b", "c"});
}

TEST_CASE("shutdown runs in reverse dependency order") {
  PluginRegistry reg;
  QStringList shutdownOrder;
  reg.registerPlugin(std::make_unique<RecorderPlugin>(
      "dependent", QStringList{"dependency"}, &shutdownOrder));
  reg.registerPlugin(std::make_unique<RecorderPlugin>(
      "dependency", QStringList{}, &shutdownOrder));
  REQUIRE(runSync(reg.shutdownAll()).has_value());
  REQUIRE(shutdownOrder == QStringList{"dependent", "dependency"});
}
