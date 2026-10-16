#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QCoro/QCoroTask>
#include <QNetworkAccessManager>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include "core/workshop/types.hpp"

namespace px::modrim::workshop {

// Trims whitespace and trailing slashes, so "https://x/" and "https://x" name the same catalog.
QString normalize_base_url(QString url);

// HTTP client for the Workshop catalog service. An empty base URL disables the catalog: every call
// fails with a CatalogError and nothing is sent.
class CatalogClient {
public:
    struct BatchResult {
        std::vector<CatalogMod> items;
        std::vector<std::string> unavailable_ids;
    };

    explicit CatalogClient(QString base_url);
    CatalogClient(const CatalogClient&) = delete;
    CatalogClient& operator=(const CatalogClient&) = delete;

    // Aborts requests still in flight; each one completes with a CatalogError.
    void shutdown();

    QCoro::Task<CatalogResult<Discover>> discover();
    QCoro::Task<CatalogResult<CatalogPage<CatalogMod>>> mods(CatalogQuery query);
    QCoro::Task<CatalogResult<CatalogPage<CatalogCollection>>> collections(CatalogQuery query);
    // nullopt when the catalog does not know the mod (HTTP 404).
    QCoro::Task<CatalogResult<std::optional<CatalogMod>>> mod(QString id);
    // nullopt when the catalog does not know the collection (HTTP 404).
    QCoro::Task<CatalogResult<std::optional<CollectionDetail>>> collection(QString id);
    QCoro::Task<CatalogResult<BatchResult>> mods_batch(QStringList ids);
    QCoro::Task<CatalogResult<ResolvedDownload>> resolve(QStringList ids, QStringList collection_ids);

private:
    struct Reply {
        int status = 0;
        std::string body;
    };

    template <class T>
    QCoro::Task<CatalogResult<T>> call(bool post, QUrl url, std::string payload = {});

    QCoro::Task<CatalogResult<Reply>> send(bool post, QUrl url, std::string payload);
    QUrl endpoint(const QString& path, const QUrlQuery& query = {}) const;

    QString base_url_;
    QNetworkAccessManager network_;
    // Requests in flight, aborted by shutdown().
    QSet<QNetworkReply*> pending_;
};

}  // namespace px::modrim::workshop
