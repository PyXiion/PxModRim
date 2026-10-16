#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>

#include <QByteArray>
#include <QCoro/QCoroTask>
#include <QDir>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "../test_support.h"
#include "core/models/metadata/structures.h"
#include "core/support/report.h"
#include "core/version.h"

using namespace px::modrim;
using namespace px::modrim::support;
using px::modrim::Result;
using test_support::TestApplication;
using test_support::write_bytes;

namespace {

// Awaits a coroutine on the event loop.
template <class T>
T wait(QCoro::Task<T> task) {
    return QCoro::waitFor(std::move(task));
}

void require_error(const Result<QString>& result, const QString& fragment) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.contains(fragment));
}

// Answers each request with `response`. An empty response never answers, which lets a test
// hit the client's timeout. The request is recorded for inspection.
class CannedServer {
public:
    explicit CannedServer(QByteArray response) : response_(std::move(response)) {
        REQUIRE(server_.listen(QHostAddress::LocalHost));
        QObject::connect(&server_, &QTcpServer::newConnection, [this] { accept(); });
    }

    QString url() const {
        return QStringLiteral("http://127.0.0.1:%1/").arg(server_.serverPort());
    }

    const QByteArray& request() const { return request_; }

private:
    void accept() {
        QTcpSocket* socket = server_.nextPendingConnection();
        socket->setParent(&server_);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
            request_ += socket->readAll();
            if (response_.isEmpty() || !complete(request_)) {
                return;
            }
            socket->write(response_);
            socket->disconnectFromHost();
        });
    }

    // True once the headers have arrived and the Content-Length body is complete.
    static bool complete(const QByteArray& raw) {
        const qsizetype headers_end = raw.indexOf("\r\n\r\n");
        if (headers_end < 0) {
            return false;
        }
        const QByteArray headers = raw.left(headers_end).toLower();
        const qsizetype key = headers.indexOf("content-length:");
        if (key < 0) {
            return true;
        }
        qsizetype eol = headers.indexOf("\r\n", key);
        if (eol < 0) {
            eol = headers.size();
        }
        const qint64 length = headers.mid(key + 15, eol - key - 15).trimmed().toLongLong();
        return raw.size() - (headers_end + 4) >= length;
    }

    QTcpServer server_;
    QByteArray response_;
    QByteArray request_;
};

QByteArray http_response(const QByteArray& status_line, const QByteArray& body) {
    return "HTTP/1.1 " + status_line + "\r\nContent-Type: text/plain\r\nContent-Length: " +
           QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

ListedModPtr about_mod(const QString& name, const QString& package_id) {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = name;
    mod->package_id = CaseInsensitiveStr(package_id);
    return mod;
}

}  // namespace

TEST_CASE("redact replaces the home directory and username in user paths") {
    CHECK(redact(QStringLiteral("User alice at /home/alice/.config/log.txt and /mnt/home/alice/x"),
                 QStringLiteral("/home/alice"), QStringLiteral("alice")) ==
          QStringLiteral("User alice at ~/.config/log.txt and /mnt/home/<user>/x"));
}

TEST_CASE("redact handles Windows-style paths") {
    CHECK(redact(QString::fromLatin1(R"(Loaded C:\Users\Bob\AppData\Local\pxmodrim\test.log)"),
                 QString::fromLatin1(R"(C:\Users\Bob)"), QStringLiteral("Bob")) ==
          QString::fromLatin1(R"(Loaded ~\AppData\Local\pxmodrim\test.log)"));
    CHECK(redact(QStringLiteral("Loaded C:/Users/Bob/AppData/test.log"),
                 QString::fromLatin1(R"(C:\Users\Bob)"), QStringLiteral("Bob")) ==
          QStringLiteral("Loaded ~/AppData/test.log"));
}

TEST_CASE("redact respects path boundaries around the home directory") {
    CHECK(redact(QStringLiteral("/home/bob/a and /home/bobby/b and /home/bob"),
                 QStringLiteral("/home/bob"), QStringLiteral("bob")) ==
          QStringLiteral("~/a and /home/bobby/b and ~"));
}

TEST_CASE("redact redacts usernames on removable media mounts") {
    CHECK(redact(QStringLiteral("/run/media/alice/SD/mods and /media/alice/x and /media/alicia/y"),
                 QStringLiteral("/home/alice"), QStringLiteral("alice")) ==
          QStringLiteral("/run/media/<user>/SD/mods and /media/<user>/x and /media/alicia/y"));
}

TEST_CASE("redact handles doubled backslashes from repr") {
    const QString raw = QString::fromLatin1(R"('C:\\Users\\Bob\\AppData' and 'D:\\Users\\Bob\\mods')");
    const QString result =
        redact(raw, QString::fromLatin1(R"(C:\Users\Bob)"), QStringLiteral("Bob"));
    CHECK_FALSE(result.contains(QStringLiteral("Bob")));
    CHECK(result.startsWith(QStringLiteral("'~")));
}

TEST_CASE("redact matches short usernames only in user directories") {
    CHECK(redact(QString::fromLatin1(R"(ed saw D:\Users\ed\mods and /Users/ED/x but not /Users/edgar)"),
                 QStringLiteral("/home/ed"), QStringLiteral("ed")) ==
          QString::fromLatin1(R"(ed saw D:\Users\<user>\mods and /Users/<user>/x but not /Users/edgar)"));
}

TEST_CASE("redact leaves package ids that contain the username alone") {
    const QString raw = QStringLiteral("Core (ludeon.rimworld) from /opt/rimworld; rimworld started");
    CHECK(redact(raw, QStringLiteral("/home/rimworld"), QStringLiteral("rimworld")) == raw);
}

TEST_CASE("read_log_tail reports a missing file") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString content =
        read_log_tail(QDir(dir.path()).filePath(QStringLiteral("does_not_exist.log")));
    CHECK(content.contains(QStringLiteral("<no log file found>")));
}

TEST_CASE("read_log_tail reports an empty file") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("empty.log"));
    REQUIRE(write_bytes(path, ""));
    CHECK(read_log_tail(path).contains(QStringLiteral("<empty log file>")));
}

TEST_CASE("read_log_tail returns a small file unchanged") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("app.log"));
    const char* text = "Line 1\nLine 2\nLine 3\n";
    REQUIRE(write_bytes(path, text));
    CHECK(read_log_tail(path) == QString::fromLatin1(text));
}

TEST_CASE("read_log_tail unpacks loguru JSON records") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("json.log"));
    REQUIRE(write_bytes(path,
                        R"({"text": "2026-09-28 | INFO | Loaded mods\n", "record": {"id": 1}})"
                        "\n"
                        "Plain log line\n"
                        R"({"text": "2026-09-28 | INFO | Ready\n", "record": {"id": 2}})"
                        "\n"));
    const QString content = read_log_tail(path);
    CHECK(content == QStringLiteral("2026-09-28 | INFO | Loaded mods\n"
                                    "Plain log line\n"
                                    "2026-09-28 | INFO | Ready\n"));
    CHECK_FALSE(content.contains(QString::fromLatin1(R"({"text")")));
}

TEST_CASE("read_log_tail keeps the end of a large file and adds a banner") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("large.log"));
    QStringList lines;
    for (int i = 0; i < 200; ++i) {
        lines.append(QStringLiteral("Log record number %1 with some extra padding").arg(i, 4, 10, QLatin1Char('0')));
    }
    REQUIRE(write_bytes(path, lines.join(QLatin1Char('\n')).toStdString()));

    const QString content = read_log_tail(path, 200);
    CHECK(content.startsWith(QStringLiteral("[... truncated, showing last ")));
    CHECK(content.contains(QStringLiteral(" of ")));
    CHECK(content.contains(QStringLiteral("Log record number 0199")));
    CHECK_FALSE(content.contains(QStringLiteral("Log record number 0001")));
}

TEST_CASE("build_report contains every section with active mods in load order") {
    TestApplication app;
    QTemporaryDir home_dir;
    REQUIRE(home_dir.isValid());
    QTemporaryDir log_dir;
    REQUIRE(log_dir.isValid());

    const QString home = QDir(home_dir.path()).filePath(QStringLiteral("home/testuser"));
    REQUIRE(QDir().mkpath(home));

    ReportSource source;
    source.game_version = QStringLiteral("1.5.4243");
    source.paths.game = QDir(home).filePath(QStringLiteral("Games/RimWorld")).toStdString();
    source.paths.config_folder = QDir(home).filePath(QStringLiteral(".config/unity3d")).toStdString();
    source.paths.local = QDir(home).filePath(QStringLiteral("Games/RimWorld/Mods")).toStdString();
    source.all_mods = {
        {QStringLiteral("u1"), about_mod(QStringLiteral("Harmony"), QStringLiteral("brrainz.harmony"))},
        {QStringLiteral("u2"), about_mod(QStringLiteral("Core"), QStringLiteral("ludeon.rimworld"))},
        {QStringLiteral("u3"), about_mod(QStringLiteral("Royalty"), QStringLiteral("ludeon.rimworld.royalty"))},
    };
    source.active_uuids = {QStringLiteral("u2"), QStringLiteral("u1"), QStringLiteral("u3")};

    const QString log_path = QDir(log_dir.path()).filePath(QStringLiteral("app.log"));
    REQUIRE(write_bytes(log_path, "INFO | Loaded /home/testuser/mods for testuser\n"));

    const QString report =
        build_report(source, log_path, home, QStringLiteral("testuser"));

    CHECK(report.contains(QStringLiteral("=== PxModRim System Report ===")));
    CHECK(report.contains(QStringLiteral("=== RimWorld Configuration ===")));
    CHECK(report.contains(QStringLiteral("=== Mod Collection ===")));
    CHECK(report.contains(QStringLiteral("=== Active Mods (Load Order) ===")));
    CHECK(report.contains(QStringLiteral("=== Application Log ===")));

    CHECK(report.contains(QStringLiteral("App Version: ") + appVersion()));
    CHECK(report.contains(QStringLiteral("Qt Version: ")));
    CHECK(report.contains(QStringLiteral("OS: ")));

    CHECK(report.contains(QStringLiteral("Game Version: 1.5.4243")));
    const QString slashed_report = QString(report).replace(u'\\', u'/');
    CHECK(slashed_report.contains(QStringLiteral("~/Games/RimWorld")));
    CHECK(slashed_report.contains(QStringLiteral("~/.config/unity3d")));
    CHECK_FALSE(report.contains(home));

    CHECK(report.contains(QStringLiteral("Total Mods: 3")));
    CHECK(report.contains(QStringLiteral("Active Mods: 3")));

    CHECK(report.contains(QStringLiteral("  1. Core (ludeon.rimworld)\n"
                                         "  2. Harmony (brrainz.harmony)\n"
                                         "  3. Royalty (ludeon.rimworld.royalty)")));

    CHECK(report.contains(QStringLiteral("Loaded /home/<user>/mods for testuser")));
}

TEST_CASE("build_report names active uuids it cannot resolve") {
    TestApplication app;
    QTemporaryDir log_dir;
    REQUIRE(log_dir.isValid());
    ReportSource source;
    source.active_uuids = {QStringLiteral("/mods/gone")};

    const QString log_path = QDir(log_dir.path()).filePath(QStringLiteral("app.log"));
    REQUIRE(write_bytes(log_path, "x\n"));
    const QString report = build_report(source, log_path);
    CHECK(report.contains(QStringLiteral("  1. Unknown Mod (/mods/gone)")));
    CHECK(report.contains(QStringLiteral("Total Mods: 0")));
}

TEST_CASE("PasteUploader posts the report and returns the URL") {
    TestApplication app;
    CannedServer server(http_response("201 Created", "https://paste.rs/abc123\n"));
    QNetworkAccessManager network;
    PasteUploader uploader(network, server.url());

    const Result<QString> result = wait(uploader.upload(QStringLiteral("Report text payload")));
    REQUIRE(result.has_value());
    CHECK(*result == QStringLiteral("https://paste.rs/abc123"));

    const QByteArray request = server.request();
    CHECK(request.contains("User-Agent: PxModRim/"));
    CHECK(request.contains("Content-Type: text/plain; charset=utf-8"));
    CHECK(request.endsWith("Report text payload"));
}

TEST_CASE("PasteUploader rejects empty text without sending it") {
    TestApplication app;
    QNetworkAccessManager network;
    PasteUploader uploader(network, QStringLiteral("http://127.0.0.1:9/"));
    require_error(wait(uploader.upload(QStringLiteral("   "))), QStringLiteral("Report text is empty"));
}

TEST_CASE("PasteUploader reports an HTTP error status") {
    TestApplication app;
    CannedServer server(http_response("403 Forbidden", "nope"));
    QNetworkAccessManager network;
    PasteUploader uploader(network, server.url());
    require_error(wait(uploader.upload(QStringLiteral("Report content"))),
                  QStringLiteral("HTTP 403 Forbidden"));
}

TEST_CASE("PasteUploader reports a server error status") {
    TestApplication app;
    CannedServer server(http_response("500 Internal Server Error", "boom"));
    QNetworkAccessManager network;
    PasteUploader uploader(network, server.url());
    require_error(wait(uploader.upload(QStringLiteral("Report content"))),
                  QStringLiteral("HTTP 500 Internal Server Error"));
}

TEST_CASE("PasteUploader rejects a success body that is not a URL") {
    TestApplication app;
    CannedServer server(http_response("200 OK", "Something unexpected went wrong"));
    QNetworkAccessManager network;
    PasteUploader uploader(network, server.url());
    require_error(wait(uploader.upload(QStringLiteral("Report content"))),
                  QStringLiteral("valid URL"));
}

TEST_CASE("PasteUploader reports a refused connection as a network error") {
    TestApplication app;
    // Listen and close to obtain a port that nothing is listening on.
    QTcpServer closed;
    REQUIRE(closed.listen(QHostAddress::LocalHost));
    const QString url = QStringLiteral("http://127.0.0.1:%1/").arg(closed.serverPort());
    closed.close();

    QNetworkAccessManager network;
    PasteUploader uploader(network, url);
    require_error(wait(uploader.upload(QStringLiteral("Report content"))),
                  QStringLiteral("network error"));
}

TEST_CASE("PasteUploader reports a request that gets no answer as a timeout") {
    TestApplication app;
    CannedServer server{QByteArray{}};
    QNetworkAccessManager network;
    PasteUploader uploader(network, server.url(), std::chrono::milliseconds(200));
    require_error(wait(uploader.upload(QStringLiteral("Report content"))),
                  QStringLiteral("timed out"));
}
