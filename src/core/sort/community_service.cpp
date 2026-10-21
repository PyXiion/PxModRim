#include "core/sort/community_service.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <algorithm>
#include <limits>
#include <zip.h>

#include <QCoro/QCoroNetworkReply>

#include <spdlog/spdlog.h>

#include "core/file_io.hpp"

namespace px::modrim::sort {

namespace {

const QString kRulesFilename = QStringLiteral("communityRules.json");
const QString kRulesUrl = QStringLiteral(
    "https://github.com/RimSort/Community-Rules-Database/archive/refs/heads/"
    "main.zip");

// A stalled transfer fails after these.
constexpr int kHeadTransferTimeoutMs = 10'000;
constexpr int kGetTransferTimeoutMs = 30'000;
constexpr int kExtractChunkBytes = 64 * 1024;

Error networkError(const QNetworkReply& reply) {
  return Error{reply.errorString()};
}

// Writes one archive entry under `dest`. Entries whose names would escape
// `dest` are skipped.
Result<void> extractEntry(zip_t* archive, zip_uint64_t index,
                          const QDir& dest) {
  zip_stat_t stat;
  zip_stat_init(&stat);
  if (zip_stat_index(archive, index, 0, &stat) != 0 ||
      !(stat.valid & ZIP_STAT_NAME)) {
    return std::unexpected(
        Error{QStringLiteral("Unreadable community rules archive entry")});
  }

  const QString name = QString::fromUtf8(stat.name);
  const QString relative = QDir::cleanPath(name);
  const bool escapes = relative == QLatin1String("..") ||
                       relative.startsWith(QLatin1String("../")) ||
                       QDir::isAbsolutePath(relative);
  if (escapes || relative == QLatin1String(".") || relative.isEmpty()) {
    return {};
  }

  if (name.endsWith(u'/')) {
    if (!dest.mkpath(relative)) {
      return std::unexpected(Error{
          QStringLiteral("Cannot create %1").arg(dest.filePath(relative))});
    }
    return {};
  }

  if (!dest.mkpath(QFileInfo(relative).path())) {
    return std::unexpected(
        Error{QStringLiteral("Cannot create directory for %1").arg(relative)});
  }
  zip_file_t* entry = zip_fopen_index(archive, index, 0);
  if (!entry) {
    return std::unexpected(
        Error{QStringLiteral("Cannot read %1 from archive").arg(name)});
  }
  auto closeEntry = qScopeGuard([entry] { zip_fclose(entry); });

  const QString target = dest.filePath(relative);
  QFile out(target);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return std::unexpected(
        Error{QStringLiteral("Cannot write %1").arg(target)});
  }

  QByteArray buffer(kExtractChunkBytes, Qt::Uninitialized);
  for (;;) {
    const zip_int64_t read = zip_fread(
        entry, buffer.data(), static_cast<zip_uint64_t>(buffer.size()));
    if (read < 0) {
      return std::unexpected(
          Error{QStringLiteral("Corrupt archive entry %1").arg(name)});
    }
    if (read == 0) {
      break;
    }
    if (out.write(buffer.constData(), read) != read) {
      return std::unexpected(
          Error{QStringLiteral("Cannot write %1").arg(target)});
    }
  }
  return {};
}

Result<void> extractZip(const QString& zipPath, const QString& destDir) {
  int errorCode = 0;
  zip_t* archive =
      zip_open(QFile::encodeName(zipPath).constData(), ZIP_RDONLY, &errorCode);
  if (!archive) {
    return std::unexpected(
        Error{QStringLiteral("Cannot open %1 (libzip error %2)")
                  .arg(zipPath)
                  .arg(errorCode)});
  }
  auto closeArchive = qScopeGuard([archive] { zip_close(archive); });

  if (!QDir().mkpath(destDir)) {
    return std::unexpected(
        Error{QStringLiteral("Cannot create %1").arg(destDir)});
  }
  const QDir dest(destDir);
  const zip_int64_t count = zip_get_num_entries(archive, 0);
  for (zip_int64_t i = 0; i < count; ++i) {
    if (auto extracted =
            extractEntry(archive, static_cast<zip_uint64_t>(i), dest);
        !extracted) {
      return extracted;
    }
  }
  return {};
}

}  // namespace

CommunityRulesService::CommunityRulesService(const ConfigService& configService,
                                             QNetworkAccessManager& network)
    : m_cacheDir(configService.directory()),
      m_jsonPath(QDir(m_cacheDir).filePath(kRulesFilename)),
      m_network(network) {}

QCoro::Task<std::optional<QString>> CommunityRulesService::ensureRules(
    LoadingState& loading) {
  auto result = co_await downloadRules(loading);
  if (!result) {
    spdlog::warn("Failed to download community rules: {}",
                 result.error().message.toStdString());
    co_return std::nullopt;
  }
  co_return *result ? std::optional<QString>(m_jsonPath) : std::nullopt;
}

// Returns false when the archive has no rules file: there is simply nothing to
// cache.
QCoro::Task<Result<bool>> CommunityRulesService::downloadRules(
    LoadingState& loading) {
  if (!QDir().mkpath(m_cacheDir)) {
    co_return std::unexpected(
        Error{QStringLiteral("Cannot create %1").arg(m_cacheDir)});
  }
  const QDir cache(m_cacheDir);
  const QString zipPath = cache.filePath(QStringLiteral("community_rules.zip"));
  const QString extractDir =
      cache.filePath(QStringLiteral("community_rules_extracted"));

  QNetworkRequest request{QUrl(kRulesUrl)};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);

  // The HEAD request only sizes the progress bar; a missing Content-Length
  // falls back to 100 steps.
  request.setTransferTimeout(kHeadTransferTimeoutMs);
  QNetworkReply* head = m_network.head(request);
  auto releaseHead = qScopeGuard([head] { head->deleteLater(); });
  co_await qCoro(head).waitForFinished();
  if (head->error() != QNetworkReply::NoError) {
    co_return std::unexpected(networkError(*head));
  }
  const qint64 contentLength =
      head->header(QNetworkRequest::ContentLengthHeader).toLongLong();
  const int totalSize =
      contentLength > 0 ? static_cast<int>(std::min<qint64>(
                              contentLength, std::numeric_limits<int>::max()))
                        : 100;

  request.setTransferTimeout(kGetTransferTimeoutMs);
  QNetworkReply* get = m_network.get(request);
  auto releaseGet = qScopeGuard([get] { get->deleteLater(); });

  QFile zipFile(zipPath);
  if (!zipFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    co_return std::unexpected(
        Error{QStringLiteral("Cannot write %1").arg(zipPath)});
  }
  {
    auto download = loading.task(
        QStringLiteral("Downloading community rules..."), totalSize);
    QObject::connect(get, &QNetworkReply::readyRead, get,
                     [get, &zipFile, &loading] {
                       const QByteArray chunk = get->readAll();
                       zipFile.write(chunk);
                       loading.step(static_cast<int>(chunk.size()));
                     });
    co_await qCoro(get).waitForFinished();
    if (get->error() != QNetworkReply::NoError) {
      co_return std::unexpected(networkError(*get));
    }
    zipFile.write(get->readAll());
    if (zipFile.error() != QFileDevice::NoError) {
      co_return std::unexpected(Error{zipFile.errorString()});
    }
    zipFile.close();
  }

  {
    auto extract = loading.task(QStringLiteral("Extracting..."), 100);
    if (auto extracted = extractZip(zipPath, extractDir); !extracted) {
      co_return std::unexpected(extracted.error());
    }
    loading.step(50);

    QDirIterator rules(extractDir, {kRulesFilename}, QDir::Files,
                       QDirIterator::Subdirectories);
    if (!rules.hasNext()) {
      // An archive without the rules file is not an error: there is simply
      // nothing to cache.
      co_return false;
    }
    if (auto copied = copyFileReplacing(rules.next(), m_jsonPath); !copied) {
      co_return std::unexpected(copied.error());
    }
    loading.step(50);
  }
  co_return true;
}

}  // namespace px::modrim::sort
