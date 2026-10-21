#include <QByteArray>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <zlib.h>

#include <catch2/catch_test_macros.hpp>

#include "core/config.hpp"
#include "core/context.hpp"
#include "core/mod_service.hpp"
#include "core/services/diagnostics_service.hpp"
#include "core/sort/models.hpp"
#include "services_support.hpp"

namespace {

QByteArray gzipText(const QByteArray& text) {
  z_stream stream{};
  REQUIRE(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                       16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
  QByteArray out(compressBound(static_cast<uLong>(text.size())) + 32, '\0');
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
  stream.avail_in = static_cast<uInt>(text.size());
  stream.next_out = reinterpret_cast<Bytef*>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  const int status = deflate(&stream, Z_FINISH);
  const auto written = out.size() - static_cast<qsizetype>(stream.avail_out);
  deflateEnd(&stream);
  REQUIRE(status == Z_STREAM_END);
  out.truncate(written);
  return out;
}

// Both rule databases must exist and be fresh, otherwise the service starts a
// background download that would outlive the test.
void writeDatabaseCaches(const QString& directory) {
  REQUIRE(services_test::writeText(
      QDir(directory).filePath(QStringLiteral("ModIdsToFix.xml")),
      "<ModIdsToFix><li>some.mod</li></ModIdsToFix>"));
  REQUIRE(services_test::writeText(
      QDir(directory).filePath(QStringLiteral("replacements.json.gz")),
      gzipText(R"({"rules":[{"oldWorkshopId":"1","newName":"New",)"
               R"("newAuthor":"Author","newPackageId":"new.mod"}]})")));
}

}  // namespace

TEST_CASE("unreadable community rules are skipped and clear loaded rules") {
  services_test::EventLoop loop;
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  writeDatabaseCaches(directory.path());
  px::modrim::ConfigService configService(directory.path());

  const QString rulesPath =
      QDir(directory.path()).filePath(QStringLiteral("communityRules.json"));
  REQUIRE(services_test::writeText(
      rulesPath, R"({"rules":{"some.mod":{"loadTop":{"value":true}}}})"));

  px::modrim::AppConfig cfg;
  cfg.paths.community_rules_file = rulesPath.toStdString();
  auto ctx = px::modrim::CoreContext::create(cfg, configService);
  auto& diagnostics = ctx->diagnosticsService();

  QStringList warnings;
  const auto id = diagnostics.warningRaised.connect(
      [&warnings](const QString& message) { warnings.append(message); });

  services_test::runAsync(diagnostics.initialize());
  CHECK(diagnostics.communityRules().contains(
      px::modrim::sort::PackageId(QStringLiteral("some.mod"))));
  CHECK(warnings.isEmpty());

  REQUIRE(services_test::writeText(rulesPath, R"({"rules": [1, 2)"));

  services_test::runAsync(diagnostics.initialize());
  CHECK(diagnostics.communityRules().isEmpty());
  CHECK(warnings.size() == 1);
  CHECK(diagnostics.warningRaised.disconnect(id));
}

TEST_CASE("a malformed community rules file does not fail the mod load") {
  services_test::EventLoop loop;
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  writeDatabaseCaches(directory.path());
  px::modrim::ConfigService configService(directory.path());

  const QString rulesPath =
      QDir(directory.path()).filePath(QStringLiteral("communityRules.json"));
  REQUIRE(services_test::writeText(rulesPath, R"({"rules": [1, 2)"));

  px::modrim::AppConfig cfg;
  cfg.paths.community_rules_file = rulesPath.toStdString();
  auto ctx = px::modrim::CoreContext::create(cfg, configService);

  CHECK(services_test::runAsync(ctx->modService().reload()).has_value());
  CHECK(ctx->diagnosticsService().communityRules().isEmpty());
}
