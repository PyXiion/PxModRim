#include "core/mods_config.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

#include <pugixml.hpp>
#include <spdlog/spdlog.h>

#include "core/constants.hpp"
#include "core/file_io.hpp"
#include "core/xml.hpp"

namespace px::modrim {

namespace {

constexpr auto kRootTag = "ModsConfigData";
constexpr auto kDefaultVersion = "1.5";
constexpr unsigned kParseFlags =
    pugi::parse_default | pugi::parse_comments | pugi::parse_pi;

std::filesystem::path toFs(const QString& path) {
  return std::filesystem::path(path.toStdU16String());
}

QString fromFs(const std::filesystem::path& path) {
  return QString::fromStdU16String(path.u16string());
}

std::optional<ModsConfig> parseModsConfigBytes(std::string_view data,
                                               const QString& source) {
  pugi::xml_document doc;
  if (const auto result =
          doc.load_buffer(data.data(), data.size(), kParseFlags);
      !result) {
    spdlog::error("Failed to parse ModsConfig.xml at {}: {}",
                  source.toStdString(), result.description());
    return std::nullopt;
  }

  const pugi::xml_node root = doc.document_element();
  const pugi::xml_node versionNode = root.child("version");
  const pugi::xml_node activeModsNode = root.child("activeMods");
  if (std::string_view(root.name()) != kRootTag || !versionNode ||
      !activeModsNode) {
    spdlog::error("Invalid ModsConfig.xml structure at {}",
                  source.toStdString());
    return std::nullopt;
  }

  // lxml's `text or "1.5"`: only a missing or empty text falls back to the
  // default.
  const QString rawVersion = rawText(versionNode);
  ModsConfig config;
  config.version = rawVersion.isEmpty() ? QString::fromLatin1(kDefaultVersion)
                                        : rawVersion.trimmed();

  for (const pugi::xml_node li : activeModsNode.children("li")) {
    if (const QString text = rawText(li).trimmed(); !text.isEmpty()) {
      config.activeMods.append(CaseInsensitiveStr(text));
    }
  }
  for (const pugi::xml_node expansions : root.children("knownExpansions")) {
    for (const pugi::xml_node li : expansions.children("li")) {
      if (const QString text = rawText(li).trimmed(); !text.isEmpty()) {
        config.knownExpansions.append(CaseInsensitiveStr(text));
      }
    }
  }
  if (config.knownExpansions.isEmpty()) {
    for (const auto& dlc : kRimworldDlcMetadata) {
      config.knownExpansions.append(CaseInsensitiveStr(QString(dlc.packageId)));
    }
  }
  return config;
}

struct SnapshotFile {
  QString path;
  std::int64_t mtimeNs = 0;
  QString name;
};

// Snapshots ordered by (mtime, name), newest first. Nanosecond mtimes match the
// legacy ordering; names break ties because they embed the creation time.
QStringList snapshotFiles(const QString& snapshotsDir) {
  // pathlib's glob is case-sensitive on POSIX, unlike QDir::match.
#ifdef Q_OS_WIN
  constexpr auto kCaseSensitivity = Qt::CaseInsensitive;
#else
  constexpr auto kCaseSensitivity = Qt::CaseSensitive;
#endif
  std::vector<SnapshotFile> found;
  std::error_code ec;
  std::filesystem::directory_iterator it(toFs(snapshotsDir), ec);
  const std::filesystem::directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const auto& entry = *it;
    const QString name = fromFs(entry.path().filename());
    std::error_code entryEc;
    if (!name.startsWith(QStringLiteral("ModsConfig_"), kCaseSensitivity) ||
        !name.endsWith(QStringLiteral(".xml"), kCaseSensitivity) ||
        !std::filesystem::is_regular_file(entry.path(), entryEc)) {
      continue;
    }
    const auto mtime = std::filesystem::last_write_time(entry.path(), entryEc);
    if (entryEc) {
      continue;
    }
    found.push_back(SnapshotFile{
        .path = fromFs(entry.path()),
        .mtimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       mtime.time_since_epoch())
                       .count(),
        .name = name,
    });
  }

  std::ranges::sort(found, [](const SnapshotFile& a, const SnapshotFile& b) {
    return std::tie(a.mtimeNs, a.name) > std::tie(b.mtimeNs, b.name);
  });
  QStringList paths;
  for (const auto& snapshot : found) {
    paths.append(snapshot.path);
  }
  return paths;
}

// Removes the oldest snapshots beyond `max_snapshots` (at least one is always
// kept), never removing `preserved`.
void pruneSnapshots(const QString& snapshotsDir, int maxSnapshots,
                    const QString& preserved = {}) {
  const QStringList snapshots = getSnapshots(snapshotsDir);
  qsizetype remaining = snapshots.size() - std::max(1, maxSnapshots);
  for (auto it = snapshots.crbegin(); it != snapshots.crend() && remaining > 0;
       ++it) {
    if (!preserved.isEmpty() &&
        QDir::cleanPath(*it) == QDir::cleanPath(preserved)) {
      continue;
    }
    --remaining;
    if (!QFile::remove(*it)) {
      spdlog::warn("Failed to remove old snapshot {}", it->toStdString());
    }
  }
}

// UTC timestamp with microseconds, e.g. 20260922_100000_123456.
std::string snapshotTimestamp() {
  using namespace std::chrono;
  const auto micros =
      duration_cast<microseconds>(system_clock::now().time_since_epoch());
  const auto seconds = floor<std::chrono::seconds>(micros);
  const auto fraction = (micros - seconds).count();
  return std::format("{:%Y%m%d_%H%M%S}_{:06}", sys_seconds{seconds}, fraction);
}

// Replaces the children named `name` with one `li` per item.
void replaceListItems(pugi::xml_node root, const char* name,
                      const QList<CaseInsensitiveStr>& items) {
  pugi::xml_node node = root.child(name);
  if (!node) {
    node = root.append_child(name);
  }
  while (const pugi::xml_node li = node.child("li")) {
    node.remove_child(li);
  }
  for (const auto& item : items) {
    node.append_child("li").text().set(item.str().toUtf8().constData());
  }
}

}  // namespace

std::optional<ModsConfig> parseModsConfig(const QString& path) {
  auto data = readFileBytes(path);
  if (!data) {
    spdlog::warn("Failed to read ModsConfig.xml at {}: {}", path.toStdString(),
                 data.error().message.toStdString());
    return std::nullopt;
  }
  return parseModsConfigBytes(*data, path);
}

QString modsConfigPath(const QString& configFolder) {
  return QDir(configFolder).filePath(QStringLiteral("ModsConfig.xml"));
}

QStringList getSnapshots(const QString& snapshotsDir) {
  return snapshotFiles(snapshotsDir);
}

Result<std::optional<QString>> createSnapshot(const QString& configPath,
                                              const QString& snapshotsDir,
                                              int maxSnapshots) {
  if (!QFileInfo(configPath).isFile()) {
    return std::optional<QString>{};
  }
  auto configData = readFileBytes(configPath);
  if (!configData) {
    spdlog::warn("Failed to read {} for snapshot: {}", configPath.toStdString(),
                 configData.error().message.toStdString());
    return std::optional<QString>{};
  }

  const QStringList snapshots = getSnapshots(snapshotsDir);
  if (!snapshots.isEmpty()) {
    if (auto newest = readFileBytes(snapshots.first());
        newest && *newest == *configData) {
      pruneSnapshots(snapshotsDir, maxSnapshots);
      return std::optional<QString>(snapshots.first());
    }
  }

  const QString timestamp = QString::fromStdString(snapshotTimestamp());
  QString snapshotPath =
      QDir(snapshotsDir)
          .filePath(QStringLiteral("ModsConfig_%1.xml").arg(timestamp));
  for (int counter = 1; QFileInfo::exists(snapshotPath); ++counter) {
    snapshotPath = QDir(snapshotsDir)
                       .filePath(QStringLiteral("ModsConfig_%1_%2.xml")
                                     .arg(timestamp)
                                     .arg(counter, 3, 10, QLatin1Char('0')));
  }

  if (auto written = writeFileAtomically(snapshotPath, *configData); !written) {
    return std::unexpected(written.error());
  }
  pruneSnapshots(snapshotsDir, maxSnapshots);
  spdlog::info("Snapshot created at {}", snapshotPath.toStdString());
  return std::optional<QString>(snapshotPath);
}

Result<void> restoreSnapshot(const QString& snapshotPath,
                             const QString& targetPath,
                             const QString& snapshotsDir, int maxSnapshots) {
  auto snapshotData = readFileBytes(snapshotPath);
  if (!snapshotData) {
    return std::unexpected(
        Error{QStringLiteral("Failed to read snapshot %1: %2")
                  .arg(snapshotPath, snapshotData.error().message)});
  }
  if (!parseModsConfigBytes(*snapshotData, snapshotPath)) {
    return std::unexpected(
        Error{QStringLiteral("Snapshot is invalid or corrupted: %1")
                  .arg(snapshotPath)});
  }

  if (QFileInfo::exists(targetPath)) {
    const int limit = std::max(1, maxSnapshots);
    if (auto kept = createSnapshot(targetPath, snapshotsDir, limit + 1);
        !kept) {
      return std::unexpected(kept.error());
    }
    pruneSnapshots(snapshotsDir, limit, snapshotPath);
  }
  if (auto written = writeFileAtomically(targetPath, *snapshotData); !written) {
    return std::unexpected(written.error());
  }
  spdlog::info("Restored snapshot {} to {}", snapshotPath.toStdString(),
               targetPath.toStdString());
  return {};
}

Result<void> writeModsConfig(const QString& path, const ModsConfig& data,
                             const QString& snapshotsDir, int maxSnapshots) {
  // An existing ModsConfigData document is updated in place so unknown XML
  // survives; anything else starts a fresh document.
  pugi::xml_document doc;
  bool reusable = false;
  if (auto existing = readFileBytes(path); existing) {
    reusable =
        doc.load_buffer(existing->data(), existing->size(), kParseFlags) &&
        std::string_view(doc.document_element().name()) == kRootTag;
  }
  if (!reusable) {
    doc.reset();
    doc.append_child(kRootTag);
  }
  pugi::xml_node root = doc.document_element();

  pugi::xml_node versionNode = root.child("version");
  if (!versionNode) {
    versionNode = root.append_child("version");
  }
  versionNode.text().set(data.version.toUtf8().constData());
  replaceListItems(root, "activeMods", data.activeMods);
  replaceListItems(root, "knownExpansions", data.knownExpansions);

  pugi::xml_node declaration = doc.prepend_child(pugi::node_declaration);
  declaration.append_attribute("version") = "1.0";
  declaration.append_attribute("encoding") = "utf-8";

  std::ostringstream out;
  doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);

  if (QFileInfo::exists(path)) {
    if (auto kept = createSnapshot(path, snapshotsDir, maxSnapshots); !kept) {
      return std::unexpected(kept.error());
    }
  }
  if (auto written = writeFileAtomically(path, out.view()); !written) {
    return std::unexpected(written.error());
  }
  spdlog::info("ModsConfig.xml written to {}", path.toStdString());
  return {};
}

}  // namespace px::modrim
