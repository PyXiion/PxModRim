#pragma once

#include <QNetworkAccessManager>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <optional>
#include <string>
#include <vector>

#include <QCoro/QCoroTask>

#include "core/workshop/types.hpp"

namespace px::modrim::workshop {

// Trims whitespace and trailing slashes, so "https://x/" and "https://x" name
// the same catalog.
QString normalizeBaseUrl(QString url);

// HTTP client for the Workshop catalog service. An empty base URL disables the
// catalog: every call fails with a CatalogError and nothing is sent.
class CatalogClient {
 public:
  explicit CatalogClient(QString baseUrl);
  CatalogClient(const CatalogClient&) = delete;
  CatalogClient& operator=(const CatalogClient&) = delete;

  // Aborts requests still in flight; each one completes with a CatalogError.
  void shutdown();

  QCoro::Task<CatalogResult<Discover>> discover();
  QCoro::Task<CatalogResult<CatalogPage<CatalogMod>>> mods(CatalogQuery query);
  QCoro::Task<CatalogResult<CatalogPage<CatalogCollection>>> collections(
      CatalogQuery query);
  // nullopt when the catalog does not know the mod (HTTP 404).
  QCoro::Task<CatalogResult<std::optional<CatalogMod>>> mod(QString id);
  // nullopt when the catalog does not know the collection (HTTP 404).
  QCoro::Task<CatalogResult<std::optional<CollectionDetail>>> collection(
      QString id);
  QCoro::Task<CatalogResult<std::vector<CatalogMod>>> modsBatch(
      QStringList ids);
  QCoro::Task<CatalogResult<ResolvedDownload>> resolve(
      QStringList ids, QStringList collectionIds);

 private:
  struct Reply {
    int status = 0;
    std::string body;
  };

  template <class T>
  QCoro::Task<CatalogResult<T>> call(bool post, QUrl url,
                                     std::string payload = {});

  QCoro::Task<CatalogResult<Reply>> send(bool post, QUrl url,
                                         std::string payload);
  QUrl endpoint(const QString& path, const QUrlQuery& query = {}) const;

  QString m_baseUrl;
  QNetworkAccessManager m_network;
  // Requests in flight, aborted by shutdown().
  QSet<QNetworkReply*> m_pending;
};

}  // namespace px::modrim::workshop
