#include "core/checker/databases.hpp"

#include <array>
#include <cmath>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zlib.h>
#include <zip.h>

#include <QCoro/QCoroFuture>
#include <QCoro/QCoroNetworkReply>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtConcurrent/QtConcurrentRun>
#include <QUrl>
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/error.hpp"
#include "core/file_io.hpp"
#include "core/xml.hpp"

namespace px::modrim::checker {
namespace {

const QString kNoVersionWarningUrl =
    QStringLiteral("https://github.com/emipa606/NoVersionWarning/archive/refs/heads/main.zip");
const QString kUseThisInsteadUrl =
    QStringLiteral("https://github.com/emipa606/UseThisInstead/archive/refs/heads/main.zip");

const QString kNoVersionWarningXml = QStringLiteral("ModIdsToFix.xml");
const QString kReplacementsGz = QStringLiteral("replacements.json.gz");

constexpr int kDownloadTimeoutMs = 30000;

std::optional<FileStamp> file_stamp(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return std::nullopt;
    }
    return FileStamp{info.lastModified().toMSecsSinceEpoch(), info.size()};
}

std::optional<double> file_age_s(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return std::nullopt;
    }
    const qint64 age_ms = info.lastModified().msecsTo(QDateTime::currentDateTime());
    return std::max(0.0, static_cast<double>(age_ms) / 1000.0);
}

// Downloads `url`, following redirects. Any transport or HTTP error fails the download.
QCoro::Task<Result<QByteArray>> fetch_archive(QNetworkAccessManager& network, const QString& url) {
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kDownloadTimeoutMs);
    QNetworkReply* reply = network.get(request);
    // Released however the coroutine leaves, so a finished reply is never kept around.
    struct Release {
        QNetworkReply* reply;
        ~Release() { reply->deleteLater(); }
    } release{reply};

    co_await qCoro(reply).waitForFinished();
    if (reply->error() != QNetworkReply::NoError) {
        co_return std::unexpected(Error{reply->errorString()});
    }
    co_return reply->readAll();
}

Error zip_failure(const char* message) {
    return Error{QString::fromUtf8(message)};
}

// Reads the first archive entry whose file name is `entry_name`, at any depth. Yields nullopt
// when no entry has that name.
Result<std::optional<QByteArray>> read_zip_entry(const QByteArray& archive,
                                                 const QString& entry_name) {
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* source = zip_source_buffer_create(archive.constData(),
                                                    static_cast<zip_uint64_t>(archive.size()),
                                                    0,
                                                    &error);
    if (source == nullptr) {
        return std::unexpected(Error{QString::fromUtf8(zip_error_strerror(&error))});
    }
    zip_t* zip = zip_open_from_source(source, ZIP_RDONLY, &error);
    if (zip == nullptr) {
        zip_source_free(source);
        return std::unexpected(Error{QString::fromUtf8(zip_error_strerror(&error))});
    }
    struct Close {
        zip_t* zip;
        ~Close() { zip_close(zip); }
    } close{zip};

    const zip_int64_t count = zip_get_num_entries(zip, 0);
    for (zip_int64_t index = 0; index < count; ++index) {
        const auto position = static_cast<zip_uint64_t>(index);
        const char* name = zip_get_name(zip, position, 0);
        if (name == nullptr ||
            QString::fromUtf8(name).section(QLatin1Char('/'), -1) != entry_name) {
            continue;
        }

        zip_file_t* file = zip_fopen_index(zip, position, 0);
        if (file == nullptr) {
            return std::unexpected(zip_failure(zip_strerror(zip)));
        }
        QByteArray bytes;
        QByteArray chunk(64 * 1024, Qt::Uninitialized);
        zip_int64_t read = 0;
        while ((read = zip_fread(file, chunk.data(), static_cast<zip_uint64_t>(chunk.size()))) > 0) {
            bytes.append(chunk.constData(), static_cast<qsizetype>(read));
        }
        const bool failed = read < 0;
        zip_fclose(file);
        if (failed) {
            return std::unexpected(zip_failure(zip_strerror(zip)));
        }
        return std::optional<QByteArray>(std::move(bytes));
    }
    return std::optional<QByteArray>{};
}

// Blocking: writes the archive entry `entry_name` to `target`. Yields false when the archive has
// no such entry, leaving `target` untouched.
Result<bool> store_archive_entry(const QByteArray& archive,
                                 const QString& entry_name,
                                 const QString& target) {
    auto entry = read_zip_entry(archive, entry_name);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    if (!entry->has_value()) {
        return false;
    }
    const QByteArray& bytes = **entry;
    auto written = write_file_atomically(
        target, std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!written) {
        return std::unexpected(written.error());
    }
    return true;
}

// Blocking: inflates a gzip stream in memory.
Result<std::string> gunzip(const std::string& data) {
    z_stream stream{};
    // 16 + MAX_WBITS selects the gzip wrapper, header and CRC included.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        return std::unexpected(Error{QStringLiteral("Cannot start gzip decoding")});
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());

    std::string out;
    std::array<char, 16384> buffer{};
    int status = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            // Z_BUF_ERROR here means the input ended before the stream did.
            inflateEnd(&stream);
            return std::unexpected(Error{QStringLiteral("Corrupt or truncated gzip data")});
        }
        out.append(buffer.data(), buffer.size() - stream.avail_out);
    } while (status != Z_STREAM_END);

    inflateEnd(&stream);
    return out;
}

// The ids under ModIdsToFix/li: a lone <li> is a string, several are an array. Nullopt when the
// document has another shape.
std::optional<QSet<PackageId>> no_version_warning_ids(const glz::json_t& root) {
    QSet<PackageId> ids;
    if (!root.contains("ModIdsToFix")) {
        return ids;
    }
    const glz::json_t& section = root["ModIdsToFix"];
    if (!section.is_object()) {
        return std::nullopt;
    }
    if (!section.contains("li")) {
        return ids;
    }
    const glz::json_t& items = section["li"];
    if (items.is_string()) {
        ids.insert(PackageId(QString::fromStdString(items.get_string())));
        return ids;
    }
    if (!items.is_array()) {
        return std::nullopt;
    }
    for (const glz::json_t& item : items.get_array()) {
        if (!item.is_string()) {
            return std::nullopt;
        }
        ids.insert(PackageId(QString::fromStdString(item.get_string())));
    }
    return ids;
}

// Blocking: parses the cached NoVersionWarning XML. A broken file yields an empty set.
QSet<PackageId> parse_no_version_warning(const QString& xml_path) {
    const auto data = xmlFileToJson(xml_path);
    if (!data) {
        spdlog::error("Failed to load No Version Warning DB: {}", data.error().message.toStdString());
        return {};
    }
    const auto ids = no_version_warning_ids(*data);
    if (!ids) {
        spdlog::error("Failed to load No Version Warning DB: unexpected ModIdsToFix layout");
        return {};
    }
    spdlog::info("Loaded {} No Version Warning entries", ids->size());
    return *ids;
}

// Text of a JSON scalar as Python's str() would show it. Other kinds yield an empty string.
QString json_text(const glz::json_t& object, const char* key) {
    if (!object.contains(key)) {
        return {};
    }
    const glz::json_t& value = object[key];
    if (value.is_string()) {
        return QString::fromStdString(value.get_string());
    }
    if (value.is_number()) {
        const double number = value.get_number();
        if (std::floor(number) == number && std::abs(number) < 1e15) {
            return QString::number(static_cast<qint64>(number));
        }
        return QString::number(number, 'g', 17);
    }
    return {};
}

QStringList json_string_list(const glz::json_t& object, const char* key) {
    QStringList values;
    if (!object.contains(key) || !object[key].is_array()) {
        return values;
    }
    for (const glz::json_t& item : object[key].get_array()) {
        if (item.is_string()) {
            values.append(QString::fromStdString(item.get_string()));
        }
    }
    return values;
}

// Blocking: parses the cached gzipped UseThisInstead JSON. A broken file yields no entries.
QHash<QString, ReplacementInfo> parse_replacements(const QString& json_path) {
    const auto bytes = read_file_bytes(json_path);
    if (!bytes) {
        spdlog::error("Failed to load Use This Instead DB: {}", bytes.error().message.toStdString());
        return {};
    }
    const auto text = gunzip(*bytes);
    if (!text) {
        spdlog::error("Failed to load Use This Instead DB: {}", text.error().message.toStdString());
        return {};
    }

    // Written by a Windows editor in some releases; the UTF-8 BOM is not part of the JSON.
    std::string body = *text;
    if (body.starts_with("\xEF\xBB\xBF")) {
        body.erase(0, 3);
    }
    glz::json_t raw;
    if (auto error = glz::read_json(raw, body)) {
        spdlog::error("Failed to load Use This Instead DB: {}", glz::format_error(error, body));
        return {};
    }

    QHash<QString, ReplacementInfo> entries;
    if (raw.is_object() && raw.contains("rules") && raw["rules"].is_array()) {
        for (const glz::json_t& rule : raw["rules"].get_array()) {
            if (!rule.is_object() || !rule.contains("oldWorkshopId")) {
                continue;
            }
            ReplacementInfo info;
            info.name = json_text(rule, "newName");
            info.author = json_text(rule, "newAuthor");
            info.packageid = json_text(rule, "newPackageId");
            info.pfid = json_text(rule, "newWorkshopId");
            info.supportedversions = json_string_list(rule, "newVersions");
            entries.insert(json_text(rule, "oldWorkshopId"), std::move(info));
        }
    }
    spdlog::info("Loaded {} Use This Instead entries", entries.size());
    return entries;
}

}  // namespace

NoVersionWarningService::NoVersionWarningService(const ConfigService& config_service,
                                                 QNetworkAccessManager& network)
    : network_(network),
      cache_dir_(config_service.directory()),
      xml_path_(QDir(cache_dir_).filePath(kNoVersionWarningXml)) {}

QCoro::Task<QSet<PackageId>> NoVersionWarningService::ensure(bool force) {
    if (!force) {
        if (const auto stamp = file_stamp(xml_path_)) {
            if (stamp != loaded_stamp_) {
                pids_ = co_await QtConcurrent::run(parse_no_version_warning, xml_path_);
                loaded_stamp_ = stamp;
            }
            co_return pids_;
        }
    }
    co_return co_await download();
}

std::optional<double> NoVersionWarningService::cache_age_s() const {
    return file_age_s(xml_path_);
}

QSet<PackageId> NoVersionWarningService::load_if_exists() {
    if (QFileInfo::exists(xml_path_)) {
        pids_ = parse_no_version_warning(xml_path_);
    }
    return pids_;
}

QCoro::Task<QSet<PackageId>> NoVersionWarningService::download() {
    QDir().mkpath(cache_dir_);

    auto archive = co_await fetch_archive(network_, kNoVersionWarningUrl);
    if (!archive) {
        spdlog::warn("Failed to download No Version Warning DB: {}",
                     archive.error().message.toStdString());
        co_return QSet<PackageId>{};
    }
    auto stored = co_await QtConcurrent::run(store_archive_entry,
                                             std::move(*archive),
                                             kNoVersionWarningXml,
                                             xml_path_);
    if (!stored) {
        spdlog::warn("Failed to download No Version Warning DB: {}",
                     stored.error().message.toStdString());
        co_return QSet<PackageId>{};
    }
    if (!*stored) {
        co_return QSet<PackageId>{};
    }

    pids_ = co_await QtConcurrent::run(parse_no_version_warning, xml_path_);
    co_return pids_;
}

UseThisInsteadService::UseThisInsteadService(const ConfigService& config_service,
                                             QNetworkAccessManager& network)
    : network_(network),
      cache_dir_(config_service.directory()),
      json_path_(QDir(cache_dir_).filePath(kReplacementsGz)) {}

QCoro::Task<QHash<QString, ReplacementInfo>> UseThisInsteadService::ensure(bool force) {
    if (!force) {
        if (const auto stamp = file_stamp(json_path_)) {
            if (stamp != loaded_stamp_) {
                entries_ = co_await QtConcurrent::run(parse_replacements, json_path_);
                loaded_stamp_ = stamp;
            }
            co_return entries_;
        }
    }
    co_return co_await download();
}

std::optional<double> UseThisInsteadService::cache_age_s() const {
    return file_age_s(json_path_);
}

QHash<QString, ReplacementInfo> UseThisInsteadService::load_if_exists() {
    if (QFileInfo::exists(json_path_)) {
        entries_ = parse_replacements(json_path_);
    }
    return entries_;
}

QCoro::Task<QHash<QString, ReplacementInfo>> UseThisInsteadService::download() {
    QDir().mkpath(cache_dir_);

    auto archive = co_await fetch_archive(network_, kUseThisInsteadUrl);
    if (!archive) {
        spdlog::warn("Failed to download Use This Instead DB: {}",
                     archive.error().message.toStdString());
        co_return QHash<QString, ReplacementInfo>{};
    }
    auto stored = co_await QtConcurrent::run(store_archive_entry,
                                             std::move(*archive),
                                             kReplacementsGz,
                                             json_path_);
    if (!stored) {
        spdlog::warn("Failed to download Use This Instead DB: {}",
                     stored.error().message.toStdString());
        co_return QHash<QString, ReplacementInfo>{};
    }
    if (!*stored) {
        co_return QHash<QString, ReplacementInfo>{};
    }

    entries_ = co_await QtConcurrent::run(parse_replacements, json_path_);
    co_return entries_;
}

}  // namespace px::modrim::checker
