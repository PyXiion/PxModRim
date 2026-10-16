#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <glaze/glaze.hpp>
#include <QByteArray>
#include <QCoro/QCoroTask>
#include <QHostAddress>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include "core/workshop/types.hpp"

namespace workshop_test {

// Awaits a coroutine on a local event loop, so network replies and disk work can complete.
template <class T>
T run(QCoro::Task<T> task) {
    return QCoro::waitFor(std::move(task));
}

// Payloads exactly as the catalog service sends them.
inline constexpr std::string_view kModJson = R"JSON({
  "id": "2009463077",
  "kind": "mod",
  "source": "steam",
  "title": "Harmony",
  "author": {
    "id": "76561198000000000",
    "name": "Author",
    "profile_url": "https://steamcommunity.com/profiles/76561198000000000"
  },
  "description": "[b]Untrusted[/b] <script>doNotExecute()</script>",
  "description_format": "bbcode",
  "preview_url": "https://images.example/harmony.jpg",
  "previews": [
    {"type": "image", "url": "https://images.example/harmony.jpg"},
    {"type": "video", "url": "https://www.youtube.com/watch?v=example"}
  ],
  "workshop_url": "https://steamcommunity.com/sharedfiles/filedetails/?id=2009463077",
  "tags": ["1.6", "Libraries"],
  "supported_versions": ["1.5", "1.6"],
  "created_at": 1600000000,
  "updated_at": 1700000000,
  "file_size": "18446744073709551615",
  "subscriptions": 12345,
  "votes": {"up": 9, "down": 1, "positive_percent": 90},
  "dependencies": ["818773962"],
  "package_id": "brrainz.harmony",
  "incompatible": false
})JSON";

inline constexpr std::string_view kCollectionJson = R"JSON({
  "id": "picked:starter",
  "kind": "collection",
  "source": "picked",
  "steam_id": null,
  "title": "Starter pack",
  "author": {"id": null, "name": "PxModRim", "profile_url": null},
  "description": "<b>Plain text</b>",
  "description_format": "text",
  "preview_url": null,
  "previews": [],
  "workshop_url": null,
  "tags": ["Quality of Life"],
  "supported_versions": ["1.6"],
  "created_at": 1600000000,
  "updated_at": 1700000000,
  "member_ids": ["2009463077"],
  "member_count": 1,
  "member_previews": [],
  "total_size": null,
  "no_common_version": false
})JSON";

inline px::modrim::workshop::CatalogMod decode_mod(std::string_view json) {
    px::modrim::workshop::CatalogMod mod;
    REQUIRE_FALSE(glz::read<px::modrim::workshop::kWireOpts>(mod, std::string(json)));
    return mod;
}

inline px::modrim::workshop::CatalogCollection decode_collection(std::string_view json) {
    px::modrim::workshop::CatalogCollection collection;
    REQUIRE_FALSE(glz::read<px::modrim::workshop::kWireOpts>(collection, std::string(json)));
    return collection;
}

// The wire form of any catalog type, for building responses.
template <class T>
std::string wire(const T& value) {
    auto text = glz::write<px::modrim::workshop::kWireOpts>(value);
    REQUIRE(text.has_value());
    return *text;
}

struct HttpRequest {
    std::string method;
    std::string target;
    std::string body;

    QUrlQuery query() const {
        return QUrlQuery(QUrl(QStringLiteral("http://localhost") + QString::fromStdString(target)));
    }
    QString path() const { return QUrl(QStringLiteral("http://localhost") + QString::fromStdString(target)).path(); }
};

struct HttpResponse {
    int status = 200;
    std::string body;
};

using Handler = std::function<HttpResponse(const HttpRequest&)>;

// A loopback catalog: answers each request with `handler`, one request per connection.
class FakeCatalog {
public:
    explicit FakeCatalog(Handler handler) : handler_(std::move(handler)) {
        REQUIRE(server_.listen(QHostAddress::LocalHost));
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] { accept(); });
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()); }

    std::vector<HttpRequest> requests;

private:
    void accept() {
        QTcpSocket* socket = server_.nextPendingConnection();
        socket->setParent(&server_);
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto raw = std::make_shared<QByteArray>();
        auto answered = std::make_shared<bool>(false);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, raw, answered] {
            *raw += socket->readAll();
            if (*answered || !complete(*raw)) {
                return;
            }
            *answered = true;
            HttpRequest request = parse(*raw);
            requests.push_back(request);
            socket->write(serialize(handler_(request)));
            socket->disconnectFromHost();
        });
    }

    // True once the headers and the Content-Length body have arrived.
    static bool complete(const QByteArray& raw) {
        const qsizetype headers_end = raw.indexOf("\r\n\r\n");
        if (headers_end < 0) {
            return false;
        }
        return raw.size() - (headers_end + 4) >= content_length(raw.left(headers_end));
    }

    static qint64 content_length(const QByteArray& headers) {
        const QByteArray lower = headers.toLower();
        const qsizetype key = lower.indexOf("content-length:");
        if (key < 0) {
            return 0;
        }
        qsizetype eol = lower.indexOf("\r\n", key);
        if (eol < 0) {
            eol = lower.size();
        }
        return lower.mid(key + 15, eol - key - 15).trimmed().toLongLong();
    }

    static HttpRequest parse(const QByteArray& raw) {
        const qsizetype headers_end = raw.indexOf("\r\n\r\n");
        const QByteArray request_line = raw.left(raw.indexOf("\r\n"));
        const QList<QByteArray> parts = request_line.split(' ');
        HttpRequest request;
        request.method = parts.value(0).toStdString();
        request.target = parts.value(1).toStdString();
        request.body = raw.mid(headers_end + 4, content_length(raw.left(headers_end))).toStdString();
        return request;
    }

    static QByteArray serialize(const HttpResponse& response) {
        return "HTTP/1.1 " + QByteArray::number(response.status) + " " + reason(response.status) +
               "\r\nContent-Type: application/json\r\nContent-Length: " +
               QByteArray::number(static_cast<qint64>(response.body.size())) +
               "\r\nConnection: close\r\n\r\n" + QByteArray::fromStdString(response.body);
    }

    static QByteArray reason(int status) {
        switch (status) {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 404: return "Not Found";
            case 500: return "Internal Server Error";
            case 502: return "Bad Gateway";
            case 503: return "Service Unavailable";
            default: return "Status";
        }
    }

    QTcpServer server_;
    Handler handler_;
};

// A URL that refuses connections: the port was bound, then released.
inline QString unreachable_url() {
    QTcpServer probe;
    REQUIRE(probe.listen(QHostAddress::LocalHost));
    const quint16 port = probe.serverPort();
    probe.close();
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}

}  // namespace workshop_test
