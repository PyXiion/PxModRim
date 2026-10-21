#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <catch2/catch_test_macros.hpp>

#include "core/loading.hpp"

namespace {

struct Emission {
  QString status;
  int pct;
  int current;
  int total;

  bool operator==(const Emission&) const = default;
};

}  // namespace

TEST_CASE(
    "loading state reports the innermost frame and pops back to its parent") {
  px::modrim::LoadingState state;
  QList<Emission> emissions;
  int finished = 0;
  QObject::connect(&state, &px::modrim::LoadingState::changed,
                   [&](const QString& status, int pct, int current, int total) {
                     emissions.append(Emission{status, pct, current, total});
                   });
  QObject::connect(&state, &px::modrim::LoadingState::finished,
                   [&] { ++finished; });

  {
    auto outer = state.task(QStringLiteral("outer"), 4);
    REQUIRE(state.statuses() == QStringList{"outer"});
    state.step(1);
    {
      auto inner = state.task(QStringLiteral("inner"));
      REQUIRE(state.statuses() == QStringList{"outer", "inner"});
      state.step(29);
    }
    REQUIRE(state.statuses() == QStringList{"outer"});
  }

  REQUIRE(state.statuses().isEmpty());
  REQUIRE(finished == 1);
  REQUIRE(emissions.last() == Emission{QStringLiteral("outer"), 25, 1, 4});
  REQUIRE(emissions.contains(Emission{QStringLiteral("inner"), 28, 29, 100}));
}

TEST_CASE("percent truncates the float quotient, as the legacy progress did") {
  px::modrim::LoadingState state;
  QList<Emission> emissions;
  QObject::connect(&state, &px::modrim::LoadingState::changed,
                   [&](const QString& status, int pct, int current, int total) {
                     emissions.append(Emission{status, pct, current, total});
                   });
  state.push(QStringLiteral("p"), 100);
  state.step(29);
  REQUIRE(emissions.last() == Emission{QStringLiteral("p"), 28, 29, 100});
}

TEST_CASE("popping an empty stack still reports completion") {
  px::modrim::LoadingState state;
  int finished = 0;
  QObject::connect(&state, &px::modrim::LoadingState::finished,
                   [&] { ++finished; });
  state.pop();
  REQUIRE(finished == 1);
}
