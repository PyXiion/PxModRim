#include "core/workshop/client.hpp"

#include <algorithm>
#include <expected>
#include <iterator>
#include <unordered_set>
#include <utility>

#include <glaze/glaze.hpp>
#include <QByteArray>
#include <QCoro/QCoroNetworkReply>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVariant>

namespace px::modrim::workshop {

namespace {

constexpr int kRequestTimeoutMs = 30000;
// Per-request limits of the catalog service; larger inputs are split across requests.
constexpr qsizetype kIdsPerBatch = 100;
constexpr qsizetype kModIdsPerResolve = 50;
constexpr qsizetype kCollectionIdsPerResolve = 10;

struct ErrorMessage {
    std::string message;
};

struct ErrorEnvelope {
    ErrorMessage error;
};

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on the wire
struct BatchWire {
    std::vector<CatalogMod> items;
    std::vector<std::string> unavailable_ids;
};
// NOLINTEND(readability-identifier-naming)

struct IdsBody {
    std::vector<std::string> ids;
};

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on the wire
struct ResolveBody {
    std::vector<std::string> ids;
    std::vector<std::string> collection_ids;
};
// NOLINTEND(readability-identifier-naming)

std::vector<std::string> toUtf8(const QStringList& list) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(list.size()));
    for (const QString& value : list) {
        out.push_back(value.toStdString());
    }
    return out;
}

// Percent-encodes everything except the unreserved characters, as Python's quote(safe="") does.
QString encodeSegment(const QString& segment) {
    return QString::fromLatin1(QUrl::toPercentEncoding(segment));
}

QUrlQuery queryOf(const CatalogQuery& query) {
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("q"), query.query);
    params.addQueryItem(QStringLiteral("sort"), query.sort);
    params.addQueryItem(QStringLiteral("limit"), QString::number(query.limit));
    params.addQueryItem(QStringLiteral("source"), query.source);
    if (query.tag) {
        params.addQueryItem(QStringLiteral("tag"), *query.tag);
    }
    if (query.version) {
        params.addQueryItem(QStringLiteral("version"), *query.version);
    }
    if (query.cursor) {
        params.addQueryItem(QStringLiteral("cursor"), *query.cursor);
    }
    return params;
}

template <class T>
std::string toJson(const T& value) {
    auto text = glz::write<kWireOpts>(value);
    Q_ASSERT(text.has_value());
    return std::move(*text);
}

template <class T>
CatalogResult<T> decode(const std::string& body, int status) {
    T value{};
    if (auto error = glz::read<kWireOpts>(value, body)) {
        return std::unexpected(CatalogError{QStringLiteral("The Workshop catalog returned an invalid response."), status});
    }
    return value;
}

// Appends the values not seen before, keeping first-occurrence order (Python dict.fromkeys).
void addUnique(std::vector<std::string>& out, std::unordered_set<std::string>& seen,
                const std::vector<std::string>& values) {
    for (const std::string& value : values) {
        if (seen.insert(value).second) {
            out.push_back(value);
        }
    }
}

}  // namespace

QString normalizeBaseUrl(QString url) {
    url = url.trimmed();
    while (url.endsWith(u'/')) {
        url.chop(1);
    }
    return url;
}

CatalogClient::CatalogClient(QString baseUrl) : m_baseUrl(normalizeBaseUrl(std::move(baseUrl))) {}

void CatalogClient::shutdown() {
    // abort() can resume waiting requests, which remove themselves from pending_: iterate a copy.
    const auto replies = m_pending;
    for (QNetworkReply* reply : replies) {
        reply->abort();
    }
}

QUrl CatalogClient::endpoint(const QString& path, const QUrlQuery& query) const {
    QUrl url(m_baseUrl + QStringLiteral("/catalog/") + path);
    url.setQuery(query);
    return url;
}

QCoro::Task<CatalogResult<CatalogClient::Reply>> CatalogClient::send(bool post, QUrl url, std::string payload) {
    if (m_baseUrl.isEmpty()) {
        co_return std::unexpected(CatalogError{QStringLiteral("Workshop catalog is disabled."), std::nullopt});
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(kRequestTimeoutMs);
    QNetworkReply* reply = nullptr;
    if (post) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        reply = m_network.post(request, QByteArray::fromStdString(payload));
    } else {
        reply = m_network.get(request);
    }
    m_pending.insert(reply);
    // Runs however the coroutine leaves, so a finished reply is never left registered for abort.
    struct Release {
        QSet<QNetworkReply*>& pending;
        QNetworkReply* reply;
        ~Release() {
            pending.remove(reply);
            reply->deleteLater();
        }
    } release{m_pending, reply};

    co_await qCoro(reply).waitForFinished();

    // An HTTP status is only present when the server answered; without one the request failed in transport.
    const QVariant statusAttribute = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusAttribute.isValid()) {
        co_return std::unexpected(CatalogError{
            QStringLiteral("Cannot reach the Workshop catalog: %1").arg(reply->errorString()), std::nullopt});
    }

    const int status = statusAttribute.toInt();
    Reply received{status, reply->readAll().toStdString()};
    if (status >= 200 && status < 300) {
        co_return received;
    }

    QString message;
    ErrorEnvelope envelope;
    if (!glz::read<kWireOpts>(envelope, received.body)) {
        message = QString::fromStdString(envelope.error.message);
    } else {
        const QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        message = reason.isEmpty()
                      ? QStringLiteral("Workshop catalog request failed (HTTP %1).").arg(status)
                      : QStringLiteral("Workshop catalog request failed (HTTP %1 %2).").arg(status).arg(reason);
    }
    co_return std::unexpected(CatalogError{message, status});
}

template <class T>
QCoro::Task<CatalogResult<T>> CatalogClient::call(bool post, QUrl url, std::string payload) {
    auto reply = co_await send(post, std::move(url), std::move(payload));
    if (!reply) {
        co_return std::unexpected(reply.error());
    }
    co_return decode<T>(reply->body, reply->status);
}

QCoro::Task<CatalogResult<Discover>> CatalogClient::discover() {
    co_return co_await call<Discover>(false, endpoint(QStringLiteral("discover")));
}

QCoro::Task<CatalogResult<CatalogPage<CatalogMod>>> CatalogClient::mods(CatalogQuery query) {
    co_return co_await call<CatalogPage<CatalogMod>>(false, endpoint(QStringLiteral("mods"), queryOf(query)));
}

QCoro::Task<CatalogResult<CatalogPage<CatalogCollection>>> CatalogClient::collections(CatalogQuery query) {
    co_return co_await call<CatalogPage<CatalogCollection>>(
        false, endpoint(QStringLiteral("collections"), queryOf(query)));
}

QCoro::Task<CatalogResult<std::optional<CatalogMod>>> CatalogClient::mod(QString id) {
    auto found = co_await call<CatalogMod>(false, endpoint(QStringLiteral("mods/") + encodeSegment(id)));
    if (found) {
        co_return std::optional<CatalogMod>(std::move(*found));
    }
    if (found.error().status == 404) {
        co_return std::optional<CatalogMod>{};
    }
    co_return std::unexpected(found.error());
}

QCoro::Task<CatalogResult<std::optional<CollectionDetail>>> CatalogClient::collection(QString id) {
    // Collection ids are "<source>:<key>"; the key may itself contain colons.
    const qsizetype colon = id.indexOf(u':');
    const QString source = colon < 0 ? QString() : id.left(colon);
    const QString key = colon < 0 ? QString() : id.mid(colon + 1);
    if (colon < 0 || key.isEmpty() ||
        (source != QStringLiteral("steam") && source != QStringLiteral("picked"))) {
        co_return std::unexpected(CatalogError{QStringLiteral("Invalid Workshop collection ID."), std::nullopt});
    }

    auto found = co_await call<CollectionDetail>(
        false, endpoint(QStringLiteral("collections/") + source + QLatin1Char('/') + encodeSegment(key)));
    if (found) {
        co_return std::optional<CollectionDetail>(std::move(*found));
    }
    if (found.error().status == 404) {
        co_return std::optional<CollectionDetail>{};
    }
    co_return std::unexpected(found.error());
}

QCoro::Task<CatalogResult<CatalogClient::BatchResult>> CatalogClient::modsBatch(QStringList ids) {
    BatchResult result;
    for (qsizetype start = 0; start < ids.size(); start += kIdsPerBatch) {
        auto batch = co_await call<BatchWire>(
            true, endpoint(QStringLiteral("mods/batch")),
            toJson(IdsBody{toUtf8(ids.mid(start, kIdsPerBatch))}));
        if (!batch) {
            co_return std::unexpected(batch.error());
        }
        result.items.insert(result.items.end(), std::make_move_iterator(batch->items.begin()),
                            std::make_move_iterator(batch->items.end()));
        result.unavailableIds.insert(result.unavailableIds.end(), batch->unavailable_ids.begin(),
                                      batch->unavailable_ids.end());
    }
    co_return result;
}

QCoro::Task<CatalogResult<ResolvedDownload>> CatalogClient::resolve(QStringList ids, QStringList collectionIds) {
    // One request covers up to 50 mods and 10 collections; the longer list sets the number of requests.
    const qsizetype batches = std::max({(ids.size() + kModIdsPerResolve - 1) / kModIdsPerResolve,
                                        (collectionIds.size() + kCollectionIdsPerResolve - 1) / kCollectionIdsPerResolve,
                                        qsizetype{1}});
    ResolvedDownload merged;
    merged.is_complete = true;
    std::unordered_set<std::string> rootsSeen;
    std::unordered_set<std::string> modIdsSeen;
    std::unordered_set<std::string> unavailableSeen;
    std::unordered_set<std::string> incompleteSeen;
    for (qsizetype batch = 0; batch < batches; ++batch) {
        ResolveBody body{
            toUtf8(ids.mid(batch * kModIdsPerResolve, kModIdsPerResolve)),
            toUtf8(collectionIds.mid(batch * kCollectionIdsPerResolve, kCollectionIdsPerResolve)),
        };
        auto resolved = co_await call<ResolvedDownload>(true, endpoint(QStringLiteral("resolve")), toJson(body));
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        addUnique(merged.roots, rootsSeen, resolved->roots);
        for (auto& [id, entry] : resolved->items) {
            merged.items.insert_or_assign(id, std::move(entry));
        }
        addUnique(merged.mod_ids, modIdsSeen, resolved->mod_ids);
        addUnique(merged.unavailable_ids, unavailableSeen, resolved->unavailable_ids);
        addUnique(merged.incomplete_collection_ids, incompleteSeen, resolved->incomplete_collection_ids);
        merged.is_complete = merged.is_complete && resolved->is_complete;
    }
    co_return merged;
}

}  // namespace px::modrim::workshop
