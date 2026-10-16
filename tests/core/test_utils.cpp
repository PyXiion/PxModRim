#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/utils.h"

namespace {

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(content);
}

}  // namespace

TEST_CASE("findAboutXml locates About/About.xml") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QDir mod(root.path());
    REQUIRE(mod.mkpath(QStringLiteral("About")));
    writeFile(mod.filePath(QStringLiteral("About/About.xml")), "<ModMetaData/>");

    const auto found = px::modrim::findAboutXml(root.path());
    REQUIRE(found.has_value());
    CHECK(found->endsWith(QStringLiteral("About/About.xml")));
}

TEST_CASE("findAboutXml matches the About folder and file name case-insensitively") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QDir mod(root.path());
    REQUIRE(mod.mkpath(QStringLiteral("ABOUT")));
    writeFile(mod.filePath(QStringLiteral("ABOUT/about.XML")), "<ModMetaData/>");

    const auto found = px::modrim::findAboutXml(root.path());
    REQUIRE(found.has_value());
    CHECK(found->endsWith(QStringLiteral("ABOUT/about.XML")));
}

TEST_CASE("findAboutXml returns nullopt when About has no About.xml") {
    QTemporaryDir root;
    REQUIRE(root.isValid());
    REQUIRE(QDir(root.path()).mkpath(QStringLiteral("About")));

    CHECK_FALSE(px::modrim::findAboutXml(root.path()).has_value());
}

TEST_CASE("findAboutXml returns nullopt for a missing directory") {
    QTemporaryDir root;
    REQUIRE(root.isValid());

    CHECK_FALSE(px::modrim::findAboutXml(root.filePath(QStringLiteral("nonexistent"))).has_value());
}

TEST_CASE("isAsciiDigits accepts only non-empty ASCII digit strings") {
    CHECK(px::modrim::isAsciiDigits(QStringLiteral("123")));
    CHECK_FALSE(px::modrim::isAsciiDigits(QStringLiteral("")));
    CHECK_FALSE(px::modrim::isAsciiDigits(QStringLiteral("12a")));
    CHECK_FALSE(px::modrim::isAsciiDigits(QStringLiteral(" 12")));
}
