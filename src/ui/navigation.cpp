#include "ui/navigation.hpp"

#include <QByteArray>
#include <QUrl>

namespace px::modrim {

QString Route::url() const { return buildRoute(view_id, path); }

QString buildRoute(const QString& viewId, const QStringList& path) {
    QString url = QStringLiteral("%1://%2").arg(QString(kRouteScheme), viewId);
    for (const QString& part : path) {
        // An empty exclusion set encodes everything but the RFC 3986 unreserved characters.
        url += QLatin1Char('/') + QString::fromLatin1(part.toUtf8().toPercentEncoding());
    }
    return url;
}

std::optional<Route> parseRoute(const QString& url) {
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.scheme() != kRouteScheme || parsed.authority().isEmpty()) {
        return std::nullopt;
    }

    // Split the encoded path first, then decode each part, so an encoded '/' stays inside its segment.
    Route route{parsed.authority(), {}};
    const QStringList encodedParts = parsed.path(QUrl::FullyEncoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : encodedParts) {
        route.path.append(QUrl::fromPercentEncoding(part.toUtf8()));
    }
    return route;
}

}  // namespace px::modrim
