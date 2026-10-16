#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QTemporaryDir>

#include "core/checker/databases.hpp"
#include "core/config.hpp"

using namespace px::modrim;
using namespace px::modrim::checker;

namespace {

std::string gzipCompress(std::string_view data) {
    z_stream stream{};
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());

    std::string out;
    std::vector<char> buffer(16384);
    int status = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = deflate(&stream, Z_FINISH);
        out.append(buffer.data(), buffer.size() - stream.avail_out);
    } while (status != Z_STREAM_END);
    deflateEnd(&stream);
    return out;
}

void writeFile(const QString& path, std::string_view content) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content.data(), static_cast<qint64>(content.size()));
}

}  // namespace

TEST_CASE("NoVersionWarningService load_if_exists on missing file returns empty") {
    QTemporaryDir dir;
    ConfigService config(dir.path());
    QNetworkAccessManager network;
    NoVersionWarningService service(config, network);

    CHECK(!service.cacheAgeS().has_value());
    CHECK(service.loadIfExists().isEmpty());
}

TEST_CASE("NoVersionWarningService parses ModIdsToFix.xml") {
    QTemporaryDir dir;
    const QString xmlPath = QDir(dir.path()).filePath(QStringLiteral("ModIdsToFix.xml"));
    writeFile(xmlPath,
               "<ModIdsToFix>"
               "  <li>brrainz.harmony</li>"
               "  <li>unlimitedhugs.hugslib</li>"
               "</ModIdsToFix>");

    ConfigService config(dir.path());
    QNetworkAccessManager network;
    NoVersionWarningService service(config, network);

    CHECK(service.cacheAgeS().has_value());
    CHECK(*service.cacheAgeS() >= 0.0);

    const auto pids = service.loadIfExists();
    CHECK(pids.size() == 2);
    CHECK(pids.contains(PackageId(QStringLiteral("brrainz.harmony"))));
    CHECK(pids.contains(PackageId(QStringLiteral("unlimitedhugs.hugslib"))));
}

TEST_CASE("NoVersionWarningService parses single-item li") {
    QTemporaryDir dir;
    const QString xmlPath = QDir(dir.path()).filePath(QStringLiteral("ModIdsToFix.xml"));
    writeFile(xmlPath, "<ModIdsToFix><li>lone.mod</li></ModIdsToFix>");

    ConfigService config(dir.path());
    QNetworkAccessManager network;
    NoVersionWarningService service(config, network);

    const auto pids = service.loadIfExists();
    CHECK(pids.size() == 1);
    CHECK(pids.contains(PackageId(QStringLiteral("lone.mod"))));
}

TEST_CASE("NoVersionWarningService handles malformed XML gracefully") {
    QTemporaryDir dir;
    const QString xmlPath = QDir(dir.path()).filePath(QStringLiteral("ModIdsToFix.xml"));
    writeFile(xmlPath, "not xml at all");

    ConfigService config(dir.path());
    QNetworkAccessManager network;
    NoVersionWarningService service(config, network);

    CHECK(service.loadIfExists().isEmpty());
}

TEST_CASE("UseThisInsteadService load_if_exists on missing file returns empty") {
    QTemporaryDir dir;
    ConfigService config(dir.path());
    QNetworkAccessManager network;
    UseThisInsteadService service(config, network);

    CHECK(!service.cacheAgeS().has_value());
    CHECK(service.loadIfExists().isEmpty());
}

TEST_CASE("UseThisInsteadService parses gzipped replacements JSON") {
    QTemporaryDir dir;
    const QString gzPath = QDir(dir.path()).filePath(QStringLiteral("replacements.json.gz"));
    const std::string json = R"({
      "rules": [
        {
          "oldWorkshopId": "123456",
          "newName": "New Mod",
          "newAuthor": "Author Guy",
          "newPackageId": "author.newmod",
          "newWorkshopId": "654321",
          "newVersions": ["1.4", "1.5"]
        }
      ]
    })";
    writeFile(gzPath, gzipCompress(json));

    ConfigService config(dir.path());
    QNetworkAccessManager network;
    UseThisInsteadService service(config, network);

    CHECK(service.cacheAgeS().has_value());
    const auto entries = service.loadIfExists();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.contains(QStringLiteral("123456")));

    const ReplacementInfo& info = entries.value(QStringLiteral("123456"));
    CHECK(info.name == QStringLiteral("New Mod"));
    CHECK(info.author == QStringLiteral("Author Guy"));
    CHECK(info.packageid == QStringLiteral("author.newmod"));
    CHECK(info.pfid == QStringLiteral("654321"));
    CHECK(info.supportedversions == QStringList{QStringLiteral("1.4"), QStringLiteral("1.5")});
    CHECK(info.source == QStringLiteral("database"));
}

TEST_CASE("UseThisInsteadService handles corrupt gzip gracefully") {
    QTemporaryDir dir;
    const QString gzPath = QDir(dir.path()).filePath(QStringLiteral("replacements.json.gz"));
    writeFile(gzPath, "not gzip data");

    ConfigService config(dir.path());
    QNetworkAccessManager network;
    UseThisInsteadService service(config, network);

    CHECK(service.loadIfExists().isEmpty());
}
