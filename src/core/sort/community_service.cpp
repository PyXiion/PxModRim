#include "core/sort/community_service.h"

#include "core/sort/community.h"

#include <algorithm>
#include <limits>

#include <QCoro/QCoroNetworkReply>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <spdlog/spdlog.h>
#include <zip.h>

#include "core/file_io.h"

namespace px::modrim::sort {

namespace {

const QString kRulesFilename = QStringLiteral("communityRules.json");
const QString kRulesUrl = QStringLiteral(
    "https://github.com/RimSort/Community-Rules-Database/archive/refs/heads/main.zip");

// Matches the httpx timeouts of the original client: a stalled transfer fails after these.
constexpr int kHeadTransferTimeoutMs = 10'000;
constexpr int kGetTransferTimeoutMs = 30'000;
constexpr int kExtractChunkBytes = 64 * 1024;

Error network_error(const QNetworkReply& reply) {
    return Error{reply.errorString()};
}

// Writes one archive entry under `dest`. Entries whose names would escape `dest` are skipped,
// as the Python zipfile sanitiser also refuses to write outside the target directory.
Result<void> extract_entry(zip_t* archive, zip_uint64_t index, const QDir& dest) {
    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(archive, index, 0, &stat) != 0 || !(stat.valid & ZIP_STAT_NAME)) {
        return std::unexpected(Error{QStringLiteral("Unreadable community rules archive entry")});
    }

    const QString name = QString::fromUtf8(stat.name);
    const QString relative = QDir::cleanPath(name);
    const bool escapes = relative == QLatin1String("..") || relative.startsWith(QLatin1String("../")) ||
                         QDir::isAbsolutePath(relative);
    if (escapes || relative == QLatin1String(".") || relative.isEmpty()) {
        return {};
    }

    if (name.endsWith(u'/')) {
        if (!dest.mkpath(relative)) {
            return std::unexpected(Error{QStringLiteral("Cannot create %1").arg(dest.filePath(relative))});
        }
        return {};
    }

    if (!dest.mkpath(QFileInfo(relative).path())) {
        return std::unexpected(Error{QStringLiteral("Cannot create directory for %1").arg(relative)});
    }
    zip_file_t* entry = zip_fopen_index(archive, index, 0);
    if (!entry) {
        return std::unexpected(Error{QStringLiteral("Cannot read %1 from archive").arg(name)});
    }
    auto close_entry = qScopeGuard([entry] { zip_fclose(entry); });

    const QString target = dest.filePath(relative);
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return std::unexpected(Error{QStringLiteral("Cannot write %1").arg(target)});
    }

    QByteArray buffer(kExtractChunkBytes, Qt::Uninitialized);
    for (;;) {
        const zip_int64_t read = zip_fread(entry, buffer.data(), static_cast<zip_uint64_t>(buffer.size()));
        if (read < 0) {
            return std::unexpected(Error{QStringLiteral("Corrupt archive entry %1").arg(name)});
        }
        if (read == 0) {
            break;
        }
        if (out.write(buffer.constData(), read) != read) {
            return std::unexpected(Error{QStringLiteral("Cannot write %1").arg(target)});
        }
    }
    return {};
}

Result<void> extract_zip(const QString& zip_path, const QString& dest_dir) {
    int error_code = 0;
    zip_t* archive = zip_open(QFile::encodeName(zip_path).constData(), ZIP_RDONLY, &error_code);
    if (!archive) {
        return std::unexpected(
            Error{QStringLiteral("Cannot open %1 (libzip error %2)").arg(zip_path).arg(error_code)});
    }
    auto close_archive = qScopeGuard([archive] { zip_close(archive); });

    if (!QDir().mkpath(dest_dir)) {
        return std::unexpected(Error{QStringLiteral("Cannot create %1").arg(dest_dir)});
    }
    const QDir dest(dest_dir);
    const zip_int64_t count = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < count; ++i) {
        if (auto extracted = extract_entry(archive, static_cast<zip_uint64_t>(i), dest); !extracted) {
            return extracted;
        }
    }
    return {};
}

}  // namespace

CommunityRulesService::CommunityRulesService(const ConfigService& config_service,
                                             QNetworkAccessManager& network)
    : cache_dir_(config_service.directory()),
      json_path_(QDir(cache_dir_).filePath(kRulesFilename)),
      network_(network) {}

QCoro::Task<std::optional<QString>> CommunityRulesService::ensure_rules(LoadingState& loading,
                                                                        bool force) {
    if (QFileInfo::exists(json_path_) && !force) {
        co_return json_path_;
    }

    auto result = co_await download_rules(loading);
    if (!result) {
        spdlog::warn("Failed to download community rules: {}", result.error().message.toStdString());
        co_return std::nullopt;
    }
    co_return *result;
}

QCoro::Task<Result<std::optional<QString>>> CommunityRulesService::download_rules(LoadingState& loading) {
    if (!QDir().mkpath(cache_dir_)) {
        co_return std::unexpected(Error{QStringLiteral("Cannot create %1").arg(cache_dir_)});
    }
    const QDir cache(cache_dir_);
    const QString zip_path = cache.filePath(QStringLiteral("community_rules.zip"));
    const QString extract_dir = cache.filePath(QStringLiteral("community_rules_extracted"));

    QNetworkRequest request{QUrl(kRulesUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    // The HEAD request only sizes the progress bar; a missing Content-Length falls back to 100
    // steps, which is what the original did.
    request.setTransferTimeout(kHeadTransferTimeoutMs);
    QNetworkReply* head = network_.head(request);
    auto release_head = qScopeGuard([head] { head->deleteLater(); });
    co_await qCoro(head).waitForFinished();
    if (head->error() != QNetworkReply::NoError) {
        co_return std::unexpected(network_error(*head));
    }
    const qint64 content_length = head->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    const int total_size = content_length > 0
                               ? static_cast<int>(std::min<qint64>(content_length,
                                                                   std::numeric_limits<int>::max()))
                               : 100;

    request.setTransferTimeout(kGetTransferTimeoutMs);
    QNetworkReply* get = network_.get(request);
    auto release_get = qScopeGuard([get] { get->deleteLater(); });

    QFile zip_file(zip_path);
    if (!zip_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        co_return std::unexpected(Error{QStringLiteral("Cannot write %1").arg(zip_path)});
    }
    {
        auto download = loading.task(QStringLiteral("Downloading community rules..."), total_size);
        QObject::connect(get, &QNetworkReply::readyRead, get, [get, &zip_file, &loading] {
            const QByteArray chunk = get->readAll();
            zip_file.write(chunk);
            loading.step(static_cast<int>(chunk.size()));
        });
        co_await qCoro(get).waitForFinished();
        if (get->error() != QNetworkReply::NoError) {
            co_return std::unexpected(network_error(*get));
        }
        zip_file.write(get->readAll());
        if (zip_file.error() != QFileDevice::NoError) {
            co_return std::unexpected(Error{zip_file.errorString()});
        }
        zip_file.close();
    }

    {
        auto extract = loading.task(QStringLiteral("Extracting..."), 100);
        if (auto extracted = extract_zip(zip_path, extract_dir); !extracted) {
            co_return std::unexpected(extracted.error());
        }
        loading.step(50);

        QDirIterator rules(extract_dir, {kRulesFilename}, QDir::Files, QDirIterator::Subdirectories);
        if (!rules.hasNext()) {
            // An archive without the rules file is not an error: there is simply nothing to cache.
            co_return std::optional<QString>{};
        }
        if (auto copied = copy_file_replacing(rules.next(), json_path_); !copied) {
            co_return std::unexpected(copied.error());
        }
        loading.step(50);
    }
    co_return std::optional<QString>(json_path_);
}

Result<CommunityRules> CommunityRulesService::load_rules() const {
    return load_community_rules(json_path_);
}

}  // namespace px::modrim::sort
