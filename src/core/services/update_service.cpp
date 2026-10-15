#include "core/services/update_service.h"

#include <algorithm>
#include <memory>

#include <QCoro/QCoroNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <spdlog/spdlog.h>

namespace px::core::services {

namespace {

constexpr int kTransferTimeoutMs = 10'000;

struct ParsedVersion {
    std::vector<qint64> numbers;
    QString suffix;
};

// Python's str.strip("-._ ") on both ends.
QString strip_separators(QString text) {
    constexpr QStringView separators = u"-._ ";
    while (!text.isEmpty() && separators.contains(text.front())) {
        text.removeFirst();
    }
    while (!text.isEmpty() && separators.contains(text.back())) {
        text.chop(1);
    }
    return text;
}

std::optional<ParsedVersion> split_version(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral(R"(^v?([0-9]+(?:\.[0-9]+)*)([^+]*))"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    ParsedVersion parsed;
    for (const QString& part : match.captured(1).split(u'.')) {
        parsed.numbers.push_back(part.toLongLong());
    }
    parsed.suffix = strip_separators(match.captured(2)).toLower();
    return parsed;
}

// Orders two versions the way the legacy key did: padded numbers, then release over
// pre-release, then the suffix text.
int compare_versions(const ParsedVersion& a, const ParsedVersion& b) {
    const std::size_t width = std::max(a.numbers.size(), b.numbers.size());
    auto padded = [width](std::vector<qint64> numbers) {
        numbers.resize(width, 0);
        return numbers;
    };
    const std::vector<qint64> left = padded(a.numbers);
    const std::vector<qint64> right = padded(b.numbers);
    if (left != right) {
        return left < right ? -1 : 1;
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

QString string_field(const QJsonObject& object, const char* key) {
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

std::optional<std::vector<qint64>> parse_version(const QString& text) {
    const auto parsed = split_version(text);
    if (!parsed) {
        return std::nullopt;
    }
    return parsed->numbers;
}

bool is_newer(const QString& tag, const QString& current) {
    const auto latest = split_version(tag);
    const auto running = split_version(current);
    if (!latest || !running) {
        return false;
    }
    return compare_versions(*latest, *running) > 0;
}

UpdateService::UpdateService(QString current_version, QNetworkAccessManager& network,
                             QString release_url)
    : current_(std::move(current_version)),
      network_(network),
      release_url_(std::move(release_url)) {}

QCoro::Task<Result<std::optional<ReleaseInfo>>> UpdateService::check() {
    QNetworkRequest request{QUrl(release_url_)};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", QStringLiteral("PxModRim/%1").arg(current_).toUtf8());
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply* pending = network_.get(request);
    std::unique_ptr<QNetworkReply, ReplyDeleter> reply(pending);
    co_await pending;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        co_return std::unexpected(Error{reply->errorString()});
    }
    if (status < 200 || status >= 300) {
        co_return std::unexpected(Error{QStringLiteral("HTTP %1 from %2").arg(status).arg(release_url_)});
    }

    const QByteArray body = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        co_return std::unexpected(Error{parseError.errorString()});
    }
    if (!document.isObject()) {
        co_return std::unexpected(Error{QStringLiteral("Latest release response is not a JSON object")});
    }

    const QJsonObject data = document.object();
    const QString tag = string_field(data, "tag_name");
    const QString url = string_field(data, "html_url");
    if (tag.isEmpty() || url.isEmpty()) {
        co_return std::unexpected(Error{QStringLiteral("Latest release response lacks tag_name/html_url")});
    }
    if (!is_newer(tag, current_)) {
        spdlog::debug("No update: latest {} vs running {}", tag.toStdString(), current_.toStdString());
        co_return std::optional<ReleaseInfo>{};
    }

    spdlog::info("Update available: {} (running {})", tag.toStdString(), current_.toStdString());
    QString name = string_field(data, "name");
    if (name.isEmpty()) {
        name = tag;
    }
    co_return std::optional<ReleaseInfo>(
        ReleaseInfo{tag, name, string_field(data, "body"), url});
}

}  // namespace px::core::services
