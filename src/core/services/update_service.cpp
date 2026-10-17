#include "core/services/update_service.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <memory>

#include <QCoro/QCoroNetworkReply>

#include <spdlog/spdlog.h>

namespace px::modrim::services {

namespace {

constexpr int kTransferTimeoutMs = 10'000;

struct ParsedVersion {
  std::vector<qint64> numbers;
  QString suffix;
};

// Python's str.strip("-._ ") on both ends.
QString stripSeparators(QString text) {
  constexpr QStringView kSeparators = u"-._ ";
  while (!text.isEmpty() && kSeparators.contains(text.front())) {
    text.removeFirst();
  }
  while (!text.isEmpty() && kSeparators.contains(text.back())) {
    text.chop(1);
  }
  return text;
}

std::optional<ParsedVersion> splitVersion(const QString& text) {
  static const QRegularExpression pattern(
      QStringLiteral(R"(^v?([0-9]+(?:\.[0-9]+)*)([^+]*))"));
  const QRegularExpressionMatch match = pattern.match(text.trimmed());
  if (!match.hasMatch()) {
    return std::nullopt;
  }
  ParsedVersion parsed;
  for (const QString& part : match.captured(1).split(u'.')) {
    parsed.numbers.push_back(part.toLongLong());
  }
  parsed.suffix = stripSeparators(match.captured(2)).toLower();
  return parsed;
}

// Orders two versions the way the legacy key did: padded numbers, then release
// over pre-release, then the suffix text.
int compareVersions(const ParsedVersion& a, const ParsedVersion& b) {
  const std::size_t width = std::max(a.numbers.size(), b.numbers.size());
  for (std::size_t i = 0; i < width; ++i) {
    const qint64 left = i < a.numbers.size() ? a.numbers[i] : 0;
    const qint64 right = i < b.numbers.size() ? b.numbers[i] : 0;
    if (left != right) {
      return left < right ? -1 : 1;
    }
  }
  const bool leftRelease = a.suffix.isEmpty();
  const bool rightRelease = b.suffix.isEmpty();
  if (leftRelease != rightRelease) {
    return leftRelease ? 1 : -1;
  }
  if (a.suffix != b.suffix) {
    return a.suffix < b.suffix ? -1 : 1;
  }
  return 0;
}

struct ReplyDeleter {
  void operator()(QNetworkReply* reply) const { reply->deleteLater(); }
};

QString stringField(const QJsonObject& object, const char* key) {
  const QJsonValue value = object.value(QLatin1StringView(key));
  if (value.isString()) {
    return value.toString();
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble());
  }
  return {};
}

}  // namespace

std::optional<std::vector<qint64>> parseVersion(const QString& text) {
  const auto parsed = splitVersion(text);
  if (!parsed) {
    return std::nullopt;
  }
  return parsed->numbers;
}

bool isNewer(const QString& tag, const QString& current) {
  const auto latest = splitVersion(tag);
  const auto running = splitVersion(current);
  if (!latest || !running) {
    return false;
  }
  return compareVersions(*latest, *running) > 0;
}

UpdateService::UpdateService(QString currentVersion,
                             QNetworkAccessManager& network, QString releaseUrl)
    : m_current(std::move(currentVersion)),
      m_network(network),
      m_releaseUrl(std::move(releaseUrl)) {}

QCoro::Task<Result<std::optional<ReleaseInfo>>> UpdateService::check() {
  QNetworkRequest request{QUrl(m_releaseUrl)};
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent",
                       QStringLiteral("PxModRim/%1").arg(m_current).toUtf8());
  request.setTransferTimeout(kTransferTimeoutMs);

  QNetworkReply* pending = m_network.get(request);
  std::unique_ptr<QNetworkReply, ReplyDeleter> reply(pending);
  co_await pending;

  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError) {
    co_return std::unexpected(Error{reply->errorString()});
  }
  if (status < 200 || status >= 300) {
    co_return std::unexpected(
        Error{QStringLiteral("HTTP %1 from %2").arg(status).arg(m_releaseUrl)});
  }

  const QByteArray body = reply->readAll();
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    co_return std::unexpected(Error{parseError.errorString()});
  }
  if (!document.isObject()) {
    co_return std::unexpected(
        Error{QStringLiteral("Latest release response is not a JSON object")});
  }

  const QJsonObject data = document.object();
  const QString tag = stringField(data, "tag_name");
  const QString url = stringField(data, "html_url");
  if (tag.isEmpty() || url.isEmpty()) {
    co_return std::unexpected(Error{
        QStringLiteral("Latest release response lacks tag_name/html_url")});
  }
  if (!isNewer(tag, m_current)) {
    spdlog::debug("No update: latest {} vs running {}", tag.toStdString(),
                  m_current.toStdString());
    co_return std::optional<ReleaseInfo>{};
  }

  spdlog::info("Update available: {} (running {})", tag.toStdString(),
               m_current.toStdString());
  QString name = stringField(data, "name");
  if (name.isEmpty()) {
    name = tag;
  }
  co_return std::optional<ReleaseInfo>(
      ReleaseInfo{tag, name, stringField(data, "body"), url});
}

}  // namespace px::modrim::services
