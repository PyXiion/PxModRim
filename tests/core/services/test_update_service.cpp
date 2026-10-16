#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <tuple>
#include <vector>

#include <QByteArray>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/services/update_service.hpp"
#include "services_support.hpp"

using namespace px::modrim;
using namespace px::modrim::services;
using services_test::EventLoop;
using services_test::runAsync;

namespace {

// Minimal local HTTP server serving a single canned response per connection.
class CannedHttpServer : public QObject {
public:
    CannedHttpServer(int statusCode, QByteArray body, QByteArray contentType = "application/json")
        : m_statusCode(statusCode), m_body(std::move(body)), m_contentType(std::move(contentType)) {
        connect(&m_server, &QTcpServer::newConnection, this, &CannedHttpServer::onNewConnection);
        REQUIRE(m_server.listen(QHostAddress::LocalHost, 0));
    }

    QString url() const {
        return QStringLiteral("http://127.0.0.1:%1/latest").arg(m_server.serverPort());
    }

private:
    void onNewConnection() {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                if (socket->canReadLine()) {
                    // Drain the headers.
                    while (!socket->readLine().trimmed().isEmpty()) {
                    }
                    const QByteArray header =
                        QByteArray("HTTP/1.1 ") + QByteArray::number(m_statusCode) + " OK\r\n" +
                        "Content-Type: " + m_contentType + "\r\n" +
                        "Content-Length: " + QByteArray::number(m_body.size()) + "\r\n" +
                        "Connection: close\r\n\r\n";
                    socket->write(header + m_body);
                    socket->disconnectFromHost();
                }
            });
        }
    }

    QTcpServer m_server;
    int m_statusCode;
    QByteArray m_body;
    QByteArray m_contentType;
};

// Returns a port on localhost that is not currently listening.
quint16 closedPort() {
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));
    const quint16 port = server.serverPort();
    server.close();
    return port;
}

constexpr auto kReleaseJson = R"({
    "tag_name": "v0.2.0",
    "html_url": "https://example.test/r/v0.2.0",
    "name": "Second",
    "body": "notes"
})";

Result<std::optional<ReleaseInfo>> checkPayload(const QByteArray& payload, int status = 200,
                                                 const QString& current = QStringLiteral("0.1.0")) {
    EventLoop loop;
    QNetworkAccessManager network;
    CannedHttpServer server(status, payload);
    UpdateService service(current, network, server.url());
    return runAsync(service.check());
}

}  // namespace

TEST_CASE("is_newer compares versions with padding, pre-release suffixes and dev versions") {
    struct Case {
        const char* tag;
        const char* current;
        bool expected;
    };
    const std::vector<Case> cases = {
        {"v0.2.0", "0.1.0", true},
        {"v0.1.0", "0.1.0", false},
        {"v0.1.0", "0.2.0", false},
        {"v0.10.0", "0.9.0", true},
        {"v1.0", "1.0.0", false},
        {"v1.0.1", "1.0", true},
        {"v0.2.0-beta.1", "0.1.0", true},
        {"nightly", "0.1.0", false},
        {"v0.2.0", "unknown", false},
        {"v0.2.0", "0.2.0-rc1", true},
        {"v0.2.0-rc1", "0.2.0", false},
        {"v0.2.0-rc2", "0.2.0-rc1", true},
        {"v0.2.0-rc1", "0.2.0-rc1", false},
        {"v0.2.0-rc1", "0.1.9", true},
        {"v0.1.9", "0.2.0-rc1", false},
        {"v0.2.0+build5", "0.2.0", false},
    };
    for (const auto& [tag, current, expected] : cases) {
        CAPTURE(tag, current);
        CHECK(isNewer(QString::fromLatin1(tag), QString::fromLatin1(current)) == expected);
    }
}

TEST_CASE("parse_version extracts leading numeric parts and rejects non-numeric tags") {
    CHECK(parseVersion(QStringLiteral("latest")) == std::nullopt);
    const auto parsed = parseVersion(QStringLiteral(" v1.2.3 "));
    REQUIRE(parsed.has_value());
    CHECK(*parsed == std::vector<qint64>{1, 2, 3});
}

TEST_CASE("UpdateService returns a newer release") {
    const auto result = checkPayload(kReleaseJson);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    const ReleaseInfo& release = **result;
    CHECK(release.tag == QStringLiteral("v0.2.0"));
    CHECK(release.name == QStringLiteral("Second"));
    CHECK(release.notes == QStringLiteral("notes"));
    CHECK(release.url == QStringLiteral("https://example.test/r/v0.2.0"));
}

TEST_CASE("UpdateService returns nullopt when the latest release is not newer") {
    const auto result = checkPayload(kReleaseJson, 200, QStringLiteral("0.2.0"));
    REQUIRE(result.has_value());
    CHECK(!result->has_value());
}

TEST_CASE("UpdateService offers a plain release over its own rc") {
    const auto result = checkPayload(kReleaseJson, 200, QStringLiteral("0.2.0-rc1"));
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK((**result).tag == QStringLiteral("v0.2.0"));
}

TEST_CASE("UpdateService wraps HTTP status errors") {
    const auto result = checkPayload("{}", 500);
    CHECK(!result.has_value());
}

TEST_CASE("UpdateService wraps transport failures") {
    EventLoop loop;
    QNetworkAccessManager network;
    const QString deadUrl = QStringLiteral("http://127.0.0.1:%1/latest").arg(closedPort());
    UpdateService service(QStringLiteral("0.1.0"), network, deadUrl);
    const auto result = runAsync(service.check());
    CHECK(!result.has_value());
}

TEST_CASE("UpdateService wraps non-JSON bodies") {
    const auto result = checkPayload("<html>", 200);
    CHECK(!result.has_value());
}

TEST_CASE("UpdateService rejects non-object JSON payloads") {
    const std::initializer_list<QByteArray> payloads{"[]", "null", "\"tag\"", "3"};
    for (const QByteArray& payload : payloads) {
        CAPTURE(payload);
        const auto result = checkPayload(payload);
        CHECK(!result.has_value());
    }
}

TEST_CASE("UpdateService rejects responses lacking tag_name or html_url") {
    for (const QByteArray& payload : {
             QByteArray("{}"),
             QByteArray(R"({"tag_name": "v0.2.0"})"),
             QByteArray(R"({"html_url": "https://example.test/r"})"),
             QByteArray(R"({"tag_name": "", "html_url": "https://example.test/r"})"),
         }) {
        CAPTURE(payload);
        const auto result = checkPayload(payload);
        CHECK(!result.has_value());
    }
}
