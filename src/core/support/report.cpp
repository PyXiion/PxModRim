#include "core/support/report.hpp"

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
#include <algorithm>

#include <QCoro/QCoroNetworkReply>

#include "core/models/metadata/structures.hpp"
#include "core/version.hpp"

#if defined(Q_OS_UNIX)
#include <pwd.h>
#include <unistd.h>
#endif

namespace px::modrim::support {

namespace {

constexpr qint64 kMib = 1024 * 1024;
constexpr qint64 kKib = 1024;
constexpr qint64 kMinReadWindow = 10 * kMib;
// Upload responses are shown back to the user, so only a short prefix is
// echoed.
constexpr qsizetype kMaxEchoedBody = 100;

// A path component may not run into adjacent name characters, so
// "ludeon.rimworld" is never mistaken for a path that contains the username
// "rimworld".
constexpr QLatin1StringView kComponentStart{R"re((?<![\w.-]))re"};
constexpr QLatin1StringView kComponentEnd{R"re((?![\w.-]))re"};

// Roots that hold user folders. Backslashes may be doubled because repr() of a
// Windows path doubles them.
constexpr QLatin1StringView kUserDirPrefix{
    R"re((?:/home|/Users|/run/media|/media|\\{1,2}Users)(?:\\{1,2}|/))re"};

QRegularExpression unicodeRegex(
    const QString& pattern, QRegularExpression::PatternOptions options = {}) {
  return QRegularExpression(
      pattern, QRegularExpression::UseUnicodePropertiesOption | options);
}

// Mirrors getpass.getuser(): the usual environment variables first, then the
// account database.
QString currentUsername() {
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

QString formatSize(qint64 bytes) {
  if (bytes >= kMib) {
    return QString::number(
               static_cast<double>(bytes) / static_cast<double>(kMib), 'f', 1) +
           QStringLiteral(" MiB");
  }
  if (bytes >= kKib) {
    return QString::number(
               static_cast<double>(bytes) / static_cast<double>(kKib), 'f', 1) +
           QStringLiteral(" KiB");
  }
  return QString::number(bytes) + QStringLiteral(" B");
}

// Splits the way Python's str.splitlines() does: several characters end a line,
// and a trailing terminator does not start an empty line.
QStringList splitLines(const QString& text) {
  QStringList lines;
  qsizetype start = 0;
  for (qsizetype i = 0; i < text.size(); ++i) {
    const char16_t c = text[i].unicode();
    const bool boundary = c == u'\n' || c == u'\r' || c == u'\v' ||
                          c == u'\f' || c == 0x1c || c == 0x1d || c == 0x1e ||
                          c == 0x85 || c == 0x2028 || c == 0x2029;
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

// loguru writes each record as a JSON object whose "text" holds the formatted
// line. Lines that are not such records pass through unchanged.
QString logLineText(const QString& line) {
  const QString trimmed = line.trimmed();
  if (!trimmed.startsWith(u'{') || !trimmed.endsWith(u'}')) {
    return line;
  }
  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(trimmed.toUtf8(), &error);
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

QString parseLogText(const QString& rawText) {
  QStringList lines = splitLines(rawText);
  for (QString& line : lines) {
    line = logLineText(line);
  }
  QString result = lines.join(QLatin1Char('\n'));
  if (rawText.endsWith(u'\n') && !result.isEmpty()) {
    result += u'\n';
  }
  return result;
}

// Drops the partial line at the start of a tail slice.
QByteArray afterFirstNewline(const QByteArray& data) {
  const qsizetype newline = data.indexOf('\n');
  return newline < 0 ? data : data.mid(newline + 1);
}

Result<QByteArray> readRawTail(QFile& file, qint64 size, qint64 window) {
  if (!file.open(QIODevice::ReadOnly)) {
    return std::unexpected(Error{file.errorString()});
  }
  if (size <= window) {
    return file.readAll();
  }
  file.seek(size - window);
  return afterFirstNewline(file.read(window));
}

// Python's repr() of a string, for quoting part of a server response in an
// error message.
QString pyRepr(const QString& text) {
  const QChar quote =
      text.contains(u'\'') && !text.contains(u'"') ? QChar(u'"') : QChar(u'\'');
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

QString redact(const QString& text, const std::optional<QString>& homeDir,
               const std::optional<QString>& username) {
  QString result = text;

  QString home = homeDir ? *homeDir : QDir::homePath();
  while (home.endsWith(u'/') || home.endsWith(u'\\')) {
    home.chop(1);
  }
  if (!home.isEmpty()) {
    // The same directory may appear with either separator, and with doubled
    // backslashes when it was embedded in a repr().
    const QString backslashed = QString(home).replace(u'/', u'\\');
    QSet<QString> variants{
        home, QString(home).replace(u'\\', u'/'), backslashed,
        QString(backslashed).replace(u'\\', QStringLiteral("\\\\"))};
    QStringList ordered(variants.begin(), variants.end());
    // Longest first, so a variant that is a prefix of another cannot shadow it.
    std::ranges::stable_sort(ordered, {},
                             [](const QString& s) { return s.size(); });
    std::ranges::reverse(ordered);

    QStringList alternatives;
    for (const QString& variant : ordered) {
      alternatives.append(QRegularExpression::escape(variant));
    }
    const QRegularExpression homePattern = unicodeRegex(
        QString(kComponentStart) + QStringLiteral("(?:") +
        alternatives.join(u'|') + QStringLiteral(")") + QString(kComponentEnd));
    result.replace(homePattern, QStringLiteral("~"));
  }

  // Only in path contexts: a bare word match would corrupt ids such as
  // ludeon.rimworld.
  const QString user = username ? *username : currentUsername();
  if (!user.isEmpty()) {
    const QRegularExpression userPattern = unicodeRegex(
        QStringLiteral("(") + QString(kUserDirPrefix) + QStringLiteral(")") +
            QRegularExpression::escape(user) + QString(kComponentEnd),
        QRegularExpression::CaseInsensitiveOption);
    result.replace(userPattern, QStringLiteral("\\1<user>"));
  }
  return result;
}

QString readLogTail(const std::optional<QString>& logPath, qint64 maxBytes) {
  const QString path =
      logPath ? *logPath
              : QDir(configDir()).filePath(QStringLiteral("logs/pxmodrim.log"));
  const QFileInfo info(path);
  if (!info.isFile()) {
    return QStringLiteral("<no log file found>");
  }
  const qint64 size = info.size();
  if (size == 0) {
    return QStringLiteral("<empty log file>");
  }

  // JSON records shrink once unpacked, so read a wider raw window than the byte
  // budget.
  const qint64 readWindow = std::max(maxBytes * 5, kMinReadWindow);
  QFile file(path);
  auto raw = readRawTail(file, size, readWindow);
  if (!raw) {
    return QStringLiteral("<error reading log file: %1>")
        .arg(raw.error().message);
  }

  QString plain = parseLogText(QString::fromUtf8(*raw));
  const QByteArray encoded = plain.toUtf8();
  if (encoded.size() <= maxBytes) {
    return plain;
  }

  const QByteArray tail = afterFirstNewline(encoded.right(maxBytes));
  return QStringLiteral("[... truncated, showing last %1 of %2 ...]\n")
             .arg(formatSize(tail.size()), formatSize(encoded.size())) +
         QString::fromUtf8(tail);
}

QString buildReport(const ReportSource& source,
                    const std::optional<QString>& logPath,
                    const std::optional<QString>& homeDir,
                    const std::optional<QString>& username,
                    qint64 maxLogBytes) {
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
  lines << QStringLiteral("Game Version: ") + source.gameVersion;
  const auto orNone = [](const std::string& value) {
    return value.empty() ? QStringLiteral("<none>")
                         : QString::fromStdString(value);
  };
  lines << QStringLiteral("Game Path: ") + orNone(paths.game);
  lines << QStringLiteral("Config Folder: ") + orNone(paths.config_folder);
  lines << QStringLiteral("Local Mods Path: ") + orNone(paths.local);
  lines << QStringLiteral("Workshop Mods Path: ") + orNone(paths.workshop);
  lines << QString();

  lines << QStringLiteral("=== Mod Collection ===");
  lines << QStringLiteral("Total Mods: %1").arg(source.allMods.size());
  lines << QStringLiteral("Active Mods: %1").arg(source.activeUuids.size());
  lines << QString();

  lines << QStringLiteral("=== Active Mods (Load Order) ===");
  if (source.activeUuids.isEmpty()) {
    lines << QStringLiteral("  <no active mods>");
  }
  for (qsizetype i = 0; i < source.activeUuids.size(); ++i) {
    const QString& uuid = source.activeUuids[i];
    QString modName = QStringLiteral("Unknown Mod");
    QString pid = uuid;
    if (const auto found = source.allMods.constFind(uuid);
        found != source.allMods.cend()) {
      modName = (*found)->name;
      const auto* about = dynamic_cast<const AboutXmlMod*>(found->get());
      pid = about ? about->packageId.str() : QStringLiteral("Unknown");
    }
    lines << QStringLiteral("%1. %2 (%3)")
                 .arg(QString::number(i + 1).rightJustified(3), modName, pid);
  }
  lines << QString();

  lines << QStringLiteral("=== Application Log ===");
  lines << readLogTail(logPath, maxLogBytes);

  return redact(lines.join(QLatin1Char('\n')), homeDir, username);
}

PasteUploader::PasteUploader(QNetworkAccessManager& network, QString endpoint,
                             std::chrono::milliseconds timeout)
    : m_network(network), m_endpoint(std::move(endpoint)), m_timeout(timeout) {}

QCoro::Task<Result<QString>> PasteUploader::upload(QString text) {
  if (text.trimmed().isEmpty()) {
    co_return std::unexpected(Error{QStringLiteral("Report text is empty")});
  }

  QNetworkRequest request{QUrl(m_endpoint)};
  request.setRawHeader("User-Agent",
                       (QStringLiteral("PxModRim/") + appVersion()).toUtf8());
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("text/plain; charset=utf-8"));
  request.setTransferTimeout(static_cast<int>(m_timeout.count()));

  QNetworkReply* reply = m_network.post(request, text.toUtf8());
  const auto releaseReply = qScopeGuard([reply] { reply->deleteLater(); });
  co_await qCoro(reply).waitForFinished();

  if (reply->error() == QNetworkReply::TimeoutError) {
    co_return std::unexpected(
        Error{QStringLiteral("Upload failed: request timed out after %1s")
                  .arg(QString::number(m_timeout.count() / 1000.0))});
  }
  // No HTTP status means the request never got a response.
  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status == 0) {
    co_return std::unexpected(
        Error{QStringLiteral("Upload failed: network error (%1)")
                  .arg(reply->errorString())});
  }
  if (status < 200 || status >= 300) {
    const QString reason = QString::fromUtf8(
        reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute)
            .toByteArray());
    co_return std::unexpected(Error{
        reason.isEmpty() ? QStringLiteral("Upload failed: HTTP %1").arg(status)
                         : QStringLiteral("Upload failed: HTTP %1 %2")
                               .arg(status)
                               .arg(reason)});
  }
  if (reply->error() != QNetworkReply::NoError) {
    co_return std::unexpected(
        Error{QStringLiteral("Upload failed: network error (%1)")
                  .arg(reply->errorString())});
  }

  const QString body = QString::fromUtf8(reply->readAll()).trimmed();
  if (!body.startsWith(QLatin1StringView("http://")) &&
      !body.startsWith(QLatin1StringView("https://"))) {
    co_return std::unexpected(Error{
        QStringLiteral("Upload failed: server did not return a valid URL: %1")
            .arg(pyRepr(body.left(kMaxEchoedBody)))});
  }
  co_return body;
}

}  // namespace px::modrim::support
