#include "core/checker/databases.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <array>
#include <cmath>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <zip.h>
#include <zlib.h>

#include <QCoro/QCoroFuture>
#include <QCoro/QCoroNetworkReply>
#include <QtConcurrent/QtConcurrentRun>

#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/error.hpp"
#include "core/file_io.hpp"
#include "core/xml.hpp"

namespace px::modrim::checker {
namespace {

const QString kNoVersionWarningUrl = QStringLiteral(
    "https://github.com/emipa606/NoVersionWarning/archive/refs/heads/main.zip");
const QString kUseThisInsteadUrl = QStringLiteral(
    "https://github.com/emipa606/UseThisInstead/archive/refs/heads/main.zip");

const QString kNoVersionWarningXml = QStringLiteral("ModIdsToFix.xml");
const QString kReplacementsGz = QStringLiteral("replacements.json.gz");

constexpr int kDownloadTimeoutMs = 30000;

std::optional<FileStamp> fileStamp(const QString& path) {
  const QFileInfo info(path);
  if (!info.exists()) {
    return std::nullopt;
  }
  return FileStamp{info.lastModified().toMSecsSinceEpoch(), info.size()};
}

std::optional<double> fileAgeS(const QString& path) {
  const QFileInfo info(path);
  if (!info.exists()) {
    return std::nullopt;
  }
  const qint64 ageMs =
      info.lastModified().msecsTo(QDateTime::currentDateTime());
  return std::max(0.0, static_cast<double>(ageMs) / 1000.0);
}

// Downloads `url`, following redirects. Any transport or HTTP error fails the
// download.
QCoro::Task<Result<QByteArray>> fetchArchive(QNetworkAccessManager& network,
                                             const QString& url) {
  QNetworkRequest request{QUrl(url)};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(kDownloadTimeoutMs);
  QNetworkReply* reply = network.get(request);
  // Released however the coroutine leaves, so a finished reply is never kept
  // around.
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

Error zipFailure(const char* message) {
  return Error{QString::fromUtf8(message)};
}

// Reads the first archive entry whose file name is `entry_name`, at any depth.
// Yields nullopt when no entry has that name.
Result<std::optional<QByteArray>> readZipEntry(const QByteArray& archive,
                                               const QString& entryName) {
  zip_error_t error;
  zip_error_init(&error);
  zip_source_t* source = zip_source_buffer_create(
      archive.constData(), static_cast<zip_uint64_t>(archive.size()), 0,
      &error);
  if (source == nullptr) {
    return std::unexpected(
        Error{QString::fromUtf8(zip_error_strerror(&error))});
  }
  zip_t* zip = zip_open_from_source(source, ZIP_RDONLY, &error);
  if (zip == nullptr) {
    zip_source_free(source);
    return std::unexpected(
        Error{QString::fromUtf8(zip_error_strerror(&error))});
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
        QString::fromUtf8(name).section(QLatin1Char('/'), -1) != entryName) {
      continue;
    }

    zip_file_t* file = zip_fopen_index(zip, position, 0);
    if (file == nullptr) {
      return std::unexpected(zipFailure(zip_strerror(zip)));
    }
    QByteArray bytes;
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    zip_int64_t read = 0;
    while ((read = zip_fread(file, chunk.data(),
                             static_cast<zip_uint64_t>(chunk.size()))) > 0) {
      bytes.append(chunk.constData(), static_cast<qsizetype>(read));
    }
    const bool failed = read < 0;
    zip_fclose(file);
    if (failed) {
      return std::unexpected(zipFailure(zip_strerror(zip)));
    }
    return std::optional<QByteArray>(std::move(bytes));
  }
  return std::optional<QByteArray>{};
}

// Blocking: writes the archive entry `entry_name` to `target`. Yields false
// when the archive has no such entry, leaving `target` untouched.
Result<bool> storeArchiveEntry(const QByteArray& archive,
                               const QString& entryName,
                               const QString& target) {
  auto entry = readZipEntry(archive, entryName);
  if (!entry) {
    return std::unexpected(entry.error());
  }
  if (!entry->has_value()) {
    return false;
  }
  const QByteArray& bytes = **entry;
  auto written = writeFileAtomically(
      target, std::string_view(bytes.constData(),
                               static_cast<std::size_t>(bytes.size())));
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
      return std::unexpected(
          Error{QStringLiteral("Corrupt or truncated gzip data")});
    }
    out.append(buffer.data(), buffer.size() - stream.avail_out);
  } while (status != Z_STREAM_END);

  inflateEnd(&stream);
  return out;
}

// The ids under ModIdsToFix/li: a lone <li> is a string, several are an array.
// Nullopt when the document has another shape.
std::optional<QSet<PackageId>> noVersionWarningIds(const glz::json_t& root) {
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

// Blocking: parses the cached NoVersionWarning XML. A broken file yields an
// empty set.
QSet<PackageId> parseNoVersionWarning(const QString& xmlPath) {
  const auto data = xmlFileToJson(xmlPath);
  if (!data) {
    spdlog::error("Failed to load No Version Warning DB: {}",
                  data.error().message.toStdString());
    return {};
  }
  const auto ids = noVersionWarningIds(*data);
  if (!ids) {
    spdlog::error(
        "Failed to load No Version Warning DB: unexpected ModIdsToFix layout");
    return {};
  }
  spdlog::info("Loaded {} No Version Warning entries", ids->size());
  return *ids;
}

// Text of a JSON scalar as Python's str() would show it. Other kinds yield an
// empty string.
QString jsonText(const glz::json_t& object, const char* key) {
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

// Blocking: parses the cached gzipped UseThisInstead JSON. A broken file yields
// no entries.
QHash<QString, ReplacementInfo> parseReplacements(const QString& jsonPath) {
  const auto bytes = readFileBytes(jsonPath);
  if (!bytes) {
    spdlog::error("Failed to load Use This Instead DB: {}",
                  bytes.error().message.toStdString());
    return {};
  }
  const auto text = gunzip(*bytes);
  if (!text) {
    spdlog::error("Failed to load Use This Instead DB: {}",
                  text.error().message.toStdString());
    return {};
  }

  // Written by a Windows editor in some releases; the UTF-8 BOM is not part of
  // the JSON.
  std::string body = *text;
  if (body.starts_with("\xEF\xBB\xBF")) {
    body.erase(0, 3);
  }
  glz::json_t raw;
  if (auto error = glz::read_json(raw, body)) {
    spdlog::error("Failed to load Use This Instead DB: {}",
                  glz::format_error(error, body));
    return {};
  }

  QHash<QString, ReplacementInfo> entries;
  if (raw.is_object() && raw.contains("rules") && raw["rules"].is_array()) {
    for (const glz::json_t& rule : raw["rules"].get_array()) {
      if (!rule.is_object() || !rule.contains("oldWorkshopId")) {
        continue;
      }
      ReplacementInfo info;
      info.name = jsonText(rule, "newName");
      info.author = jsonText(rule, "newAuthor");
      info.packageid = jsonText(rule, "newPackageId");
      entries.insert(jsonText(rule, "oldWorkshopId"), std::move(info));
    }
  }
  spdlog::info("Loaded {} Use This Instead entries", entries.size());
  return entries;
}

}  // namespace

template <>
const NoVersionWarningDb::Source& NoVersionWarningDb::source() {
  static const Source value{QStringLiteral("No Version Warning"),
                            kNoVersionWarningUrl, kNoVersionWarningXml,
                            parseNoVersionWarning};
  return value;
}

template <>
const UseThisInsteadDb::Source& UseThisInsteadDb::source() {
  static const Source value{QStringLiteral("Use This Instead"),
                            kUseThisInsteadUrl, kReplacementsGz,
                            parseReplacements};
  return value;
}

template <typename Entries>
CachedDatabase<Entries>::CachedDatabase(const ConfigService& configService,
                                        QNetworkAccessManager& network)
    : m_network(network),
      m_cacheDir(configService.directory()),
      m_path(QDir(m_cacheDir).filePath(source().fileName)) {}

template <typename Entries>
QCoro::Task<Entries> CachedDatabase<Entries>::ensure(bool force) {
  if (!force) {
    if (const auto stamp = fileStamp(m_path)) {
      if (stamp != m_loadedStamp) {
        m_entries = co_await QtConcurrent::run(source().parse, m_path);
        m_loadedStamp = stamp;
      }
      co_return m_entries;
    }
  }
  co_return co_await download();
}

template <typename Entries>
std::optional<double> CachedDatabase<Entries>::cacheAgeS() const {
  return fileAgeS(m_path);
}

template <typename Entries>
Entries CachedDatabase<Entries>::loadIfExists() {
  if (QFileInfo::exists(m_path)) {
    m_entries = source().parse(m_path);
  }
  return m_entries;
}

template <typename Entries>
QCoro::Task<Entries> CachedDatabase<Entries>::download() {
  QDir().mkpath(m_cacheDir);

  const Source& src = source();
  auto archive = co_await fetchArchive(m_network, src.url);
  if (!archive) {
    spdlog::warn("Failed to download {} DB: {}", src.label.toStdString(),
                 archive.error().message.toStdString());
    co_return Entries{};
  }
  auto stored = co_await QtConcurrent::run(
      storeArchiveEntry, std::move(*archive), src.fileName, m_path);
  if (!stored) {
    spdlog::warn("Failed to download {} DB: {}", src.label.toStdString(),
                 stored.error().message.toStdString());
    co_return Entries{};
  }
  if (!*stored) {
    co_return Entries{};
  }

  m_entries = co_await QtConcurrent::run(src.parse, m_path);
  co_return m_entries;
}

template class CachedDatabase<QSet<PackageId>>;
template class CachedDatabase<QHash<QString, ReplacementInfo>>;

}  // namespace px::modrim::checker
