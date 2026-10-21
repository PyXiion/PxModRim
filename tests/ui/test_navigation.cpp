#include <QString>
#include <QStringList>

#include <catch2/catch_test_macros.hpp>

#include "ui/navigation.hpp"

using px::modrim::buildRoute;
using px::modrim::parseRoute;
using px::modrim::Route;

TEST_CASE("route round-trips ids with reserved characters") {
  const QString url =
      buildRoute(QStringLiteral("workshop"),
                 {QStringLiteral("collection"), QStringLiteral("picked:a/b")});
  CHECK(url == QStringLiteral("modrim://workshop/collection/picked%3Aa%2Fb"));
  CHECK(parseRoute(url) ==
        Route{QStringLiteral("workshop"),
              {QStringLiteral("collection"), QStringLiteral("picked:a/b")}});
}

TEST_CASE("view-only route has an empty path") {
  CHECK(parseRoute(QStringLiteral("modrim://downloads")) ==
        Route{QStringLiteral("downloads"), {}});
  CHECK(parseRoute(QStringLiteral("modrim://downloads/")) ==
        Route{QStringLiteral("downloads"), {}});
}

TEST_CASE("non-routes are rejected") {
  for (const char* url : {"downloads", "https://downloads", "modrim://", ""}) {
    CAPTURE(url);
    CHECK_FALSE(parseRoute(QString::fromUtf8(url)).has_value());
  }
}
