#include "core/support/report.h"

#include <algorithm>
#include <array>

#include <QCoro/QCoroNetworkReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSysInfo>
#include <QUrl>
#include <QtGlobal>

#include "core/models/metadata/structures.h"
#include "core/version.h"

#if defined(Q_OS_UNIX)
#include <pwd.h>
#include <unistd.h>
#endif

namespace px::modrim::support {

namespace {

constexpr qint64 MIB = 1024 * 1024;
constexpr qint64 KIB = 1024;
constexpr qint64 MIN_READ_WINDOW = 10 * MIB;
// Upload responses are shown back to the user, so only a short prefix is echoed.
constexpr qsizetype MAX_ECHOED_BODY = 100;

// A path component may not run into adjacent name characters, so "ludeon.rimworld" is never
// mistaken for a path that contains the username "rimworld".
constexpr QLatin1StringView COMPONENT_START{R"re((?<![\w.-]))re"};
constexpr QLatin1StringView COMPONENT_END{R"re((?![\w.-]))re"};

// Roots that hold user folders. Backslashes may be doubled because repr() of a Windows path
// doubles them.
constexpr QLatin1StringView USER_DIR_PREFIX{R"re((?:/home|/Users|/run/media|/media|\\{1,2}Users)(?:\\{1,2}|/))re"};

QRegularExpression unicode_regex(const QString& pattern,
                                 QRegularExpression::PatternOptions options = {}) {
    return QRegularExpression(pattern,
                              QRegularExpression::UseUnicodePropertiesOption | options);
}

// Mirrors getpass.getuser(): the usual environment variables first, then the account database.
QString current_username() {
    for (const char* name : {"LOGNAME", "USER", "LNAME", "USERNAME"}) {
        if (const QString value = qEnvironmentVariable(name); !value.isEmpty()) {
            return value;
        }
    }
#if defined(Q_OS_UNIX)
    if (const passwd* entry = getpwuid(getuid())) {
        return QString::fromLocal8Bit(entry->pw_name);
    }
#endif
    return QString();
}

QString format_size(qint64 bytes) {
    if (bytes >= MIB) {
        return QString::number(static_cast<double>(bytes) / static_cast<double>(MIB), 'f', 1) +
               QStringLiteral(" MiB");
    }
    if (bytes >= KIB) {
        return QString::number(static_cast<double>(bytes) / static_cast<double>(KIB), 'f', 1) +
               QStringLiteral(" KiB");
    }
    return QString::number(bytes) + QStringLiteral(" B");
}

// Splits the way Python's str.splitlines() does: several characters end a line, and a trailing
// terminator does not start an empty line.
QStringList split_lines(const QString& text) {
    QStringList lines;
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t c = text[i].unicode();
        const bool boundary = c == u'\n' || c == u'\r' || c == u'\v' || c == u'\f' ||
                              c == 0x1c || c == 0x1d || c == 0x1e || c == 0x85 || c == 0x2028 ||
                              c == 0x2029;
        if (!boundary) {
            continue;
        }
        lines.append(text.mid(start, i - start));
        if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') {
            ++i;
        }
        start = i + 1;
    }
    if (start < text.size()) {
        lines.append(text.mid(start));
    }
    return lines;
}

// loguru writes each record as a JSON object whose "text" holds the formatted line. Lines
// that are not such records pass through unchanged.
QString log_line_text(const QString& line) {
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(u'{') || !trimmed.endsWith(u'}')) {
        return line;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return line;
    }
    const QJsonValue text = document.object().value(QStringLiteral("text"));
    if (!text.isString()) {
        return line;
    }
    QString result = text.toString();
    while (result.endsWith(u'\r') || result.endsWith(u'\n')) {
        result.chop(1);
    }
    return result;
}

QString parse_log_text(const QString& raw_text) {
    QStringList lines = split_lines(raw_text);
    for (QString& line : lines) {
        line = log_line_text(line);
    }
    QString result = lines.join(QLatin1Char('\n'));
    if (raw_text.endsWith(u'\n') && !result.isEmpty()) {
        result += u'\n';
    }
    return result;
}

// Drops the partial line at the start of a tail slice.
QByteArray after_first_newline(const QByteArray& data) {
    const qsizetype newline = data.indexOf('\n');
    return newline < 0 ? data : data.mid(newline + 1);
}

Result<QByteArray> read_raw_tail(QFile& file, qint64 size, qint64 window) {
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(Error{file.errorString()});
    }
    if (size <= window) {
        return file.readAll();
    }
    file.seek(size - window);
    return after_first_newline(file.read(window));
}

// Python's repr() of a string, for quoting part of a server response in an error message.
QString py_repr(const QString& text) {
    const QChar quote = text.contains(u'\'') && !text.contains(u'"') ? QChar(u'"') : QChar(u'\'');
    QString out(quote);
    for (const QChar c : text) {
        switch (c.unicode()) {
            case u'\\':
                out += QStringLiteral("\\\\");
                break;
            case u'\n':
                out += QStringLiteral("\\n");
                break;
            case u'\r':
                out += QStringLiteral("\\r");
                break;
            case u'\t':
                out += QStringLiteral("\\t");
                break;
            default:
                if (c == quote) {
                    out += u'\\';
                }
                out += c;
        }
    }
    out += quote;
    return out;
}

}  // namespace

QString redact(const QString& text, const std::optional<QString>& home_dir,
               const std::optional<QString>& username) {
    QString result = text;

    QString home = home_dir ? *home_dir : QDir::homePath();
    while (home.endsWith(u'/') || home.endsWith(u'\\')) {
        home.chop(1);
    }
    if (!home.isEmpty()) {
        // The same directory may appear with either separator, and with doubled backslashes
        // when it was embedded in a repr().
        const QString backslashed = QString(home).replace(u'/', u'\\');
        QSet<QString> variants{home, QString(home).replace(u'\\', u'/'), backslashed,
                               QString(backslashed).replace(u'\\', QStringLiteral("\\\\"))};
        QStringList ordered(variants.begin(), variants.end());
        // Longest first, so a variant that is a prefix of another cannot shadow it.
        std::ranges::stable_sort(ordered, {}, [](const QString& s) { return s.size(); });
        std::ranges::reverse(ordered);

        QStringList alternatives;
        for (const QString& variant : ordered) {
            alternatives.append(QRegularExpression::escape(variant));
        }
        const QRegularExpression home_pattern = unicode_regex(
            QString(COMPONENT_START) + QStringLiteral("(?:") + alternatives.join(u'|') +
            QStringLiteral(")") + QString(COMPONENT_END));
        result.replace(home_pattern, QStringLiteral("~"));
    }

    // Only in path contexts: a bare word match would corrupt ids such as ludeon.rimworld.
    const QString user = username ? *username : current_username();
    if (!user.isEmpty()) {
        const QRegularExpression user_pattern = unicode_regex(
            QStringLiteral("(") + QString(USER_DIR_PREFIX) + QStringLiteral(")") +
                QRegularExpression::escape(user) + QString(COMPONENT_END),
            QRegularExpression::CaseInsensitiveOption);
        result.replace(user_pattern, QStringLiteral("\\1<user>"));
    }
    return result;
}

QString read_log_tail(const std::optional<QString>& log_path, qint64 max_bytes) {
    const QString path = log_path ? *log_path
                                  : QDir(config_dir()).filePath(QStringLiteral("logs/pxmodrim.log"));
    const QFileInfo info(path);
    if (!info.isFile()) {
        return QStringLiteral("<no log file found>");
    }
    const qint64 size = info.size();
    if (size == 0) {
        return QStringLiteral("<empty log file>");
    }

    // JSON records shrink once unpacked, so read a wider raw window than the byte budget.
    const qint64 read_window = std::max(max_bytes * 5, MIN_READ_WINDOW);
    QFile file(path);
    auto raw = read_raw_tail(file, size, read_window);
    if (!raw) {
        return QStringLiteral("<error reading log file: %1>").arg(raw.error().message);
    }

    const QString plain = parse_log_text(QString::fromUtf8(*raw));
    const QByteArray encoded = plain.toUtf8();
    if (encoded.size() <= max_bytes) {
        return plain;
    }

    const QByteArray tail = after_first_newline(encoded.right(max_bytes));
    return QStringLiteral("[... truncated, showing last %1 of %2 ...]\n")
               .arg(format_size(tail.size()), format_size(encoded.size())) +
           QString::fromUtf8(tail);
}

QString build_report(const ReportSource& source, const std::optional<QString>& log_path,
                     const std::optional<QString>& home_dir, const std::optional<QString>& username,
                     qint64 max_log_bytes) {
    QStringList lines;

    lines << QStringLiteral("=== PxModRim System Report ===");
    lines << QStringLiteral("App Version: ") + appVersion();
    lines << QStringLiteral("Qt Version: ") + QString::fromLatin1(qVersion());
    lines << QStringLiteral("OS: %1 %2 (%3)")
                 .arg(QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                      QSysInfo::currentCpuArchitecture());
    lines << QString();

    const PathConfig& paths = source.paths;
    lines << QStringLiteral("=== RimWorld Configuration ===");
    lines << QStringLiteral("Game Version: ") + source.game_version;
    const auto or_none = [](const std::string& value) {
        return value.empty() ? QStringLiteral("<none>") : QString::fromStdString(value);
    };
    lines << QStringLiteral("Game Path: ") + or_none(paths.game);
    lines << QStringLiteral("Config Folder: ") + or_none(paths.config_folder);
    lines << QStringLiteral("Local Mods Path: ") + or_none(paths.local);
    lines << QStringLiteral("Workshop Mods Path: ") + or_none(paths.workshop);
    lines << QString();

    lines << QStringLiteral("=== Mod Collection ===");
    lines << QStringLiteral("Total Mods: %1").arg(source.all_mods.size());
    lines << QStringLiteral("Active Mods: %1").arg(source.active_uuids.size());
    lines << QString();

    lines << QStringLiteral("=== Active Mods (Load Order) ===");
    if (source.active_uuids.isEmpty()) {
        lines << QStringLiteral("  <no active mods>");
    }
    for (qsizetype i = 0; i < source.active_uuids.size(); ++i) {
        const QString& uuid = source.active_uuids[i];
        QString mod_name = QStringLiteral("Unknown Mod");
        QString pid = uuid;
        if (const auto found = source.all_mods.constFind(uuid); found != source.all_mods.cend()) {
            mod_name = (*found)->name;
            const auto* about = dynamic_cast<const AboutXmlMod*>(found->get());
            pid = about ? about->package_id.str() : QStringLiteral("Unknown");
        }
        lines << QStringLiteral("%1. %2 (%3)")
                     .arg(QString::number(i + 1).rightJustified(3), mod_name, pid);
    }
    lines << QString();

    lines << QStringLiteral("=== Application Log ===");
    lines << read_log_tail(log_path, max_log_bytes);

    return redact(lines.join(QLatin1Char('\n')), home_dir, username);
}

PasteUploader::PasteUploader(QNetworkAccessManager& network, QString endpoint,
                             std::chrono::milliseconds timeout)
    : network_(network), endpoint_(std::move(endpoint)), timeout_(timeout) {}

QCoro::Task<Result<QString>> PasteUploader::upload(QString text) {
    if (text.trimmed().isEmpty()) {
        co_return std::unexpected(Error{QStringLiteral("Report text is empty")});
    }

    QNetworkRequest request{QUrl(endpoint_)};
    request.setRawHeader("User-Agent", (QStringLiteral("PxModRim/") + appVersion()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("text/plain; charset=utf-8"));
    request.setTransferTimeout(static_cast<int>(timeout_.count()));

    QNetworkReply* reply = network_.post(request, text.toUtf8());
    const auto release_reply = qScopeGuard([reply] { reply->deleteLater(); });
    co_await qCoro(reply).waitForFinished();

    if (reply->error() == QNetworkReply::TimeoutError) {
        co_return std::unexpected(Error{QStringLiteral("Upload failed: request timed out after %1s")
                                            .arg(QString::number(timeout_.count() / 1000.0))});
    }
    // No HTTP status means the request never got a response.
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0) {
        co_return std::unexpected(Error{QStringLiteral("Upload failed: network error (%1)")
                                            .arg(reply->errorString())});
    }
    if (status < 200 || status >= 300) {
        const QString reason = QString::fromUtf8(
            reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toByteArray());
        co_return std::unexpected(Error{
            reason.isEmpty() ? QStringLiteral("Upload failed: HTTP %1").arg(status)
                             : QStringLiteral("Upload failed: HTTP %1 %2").arg(status).arg(reason)});
    }
    if (reply->error() != QNetworkReply::NoError) {
        co_return std::unexpected(Error{QStringLiteral("Upload failed: network error (%1)")
                                            .arg(reply->errorString())});
    }

    const QString body = QString::fromUtf8(reply->readAll()).trimmed();
    if (!body.startsWith(QLatin1StringView("http://")) &&
        !body.startsWith(QLatin1StringView("https://"))) {
        co_return std::unexpected(Error{
            QStringLiteral("Upload failed: server did not return a valid URL: %1")
                .arg(py_repr(body.left(MAX_ECHOED_BODY)))});
    }
    co_return body;
}

}  // namespace px::modrim::support
