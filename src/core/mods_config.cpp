#include "core/mods_config.hpp"

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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <pugixml.hpp>
#include <spdlog/spdlog.h>

#include "core/constants.hpp"
#include "core/file_io.hpp"
#include "core/xml.hpp"

namespace px::modrim {

namespace {

constexpr auto kRootTag = "ModsConfigData";
constexpr auto kDefaultVersion = "1.5";
constexpr unsigned kParseFlags = pugi::parse_default | pugi::parse_comments | pugi::parse_pi;

std::filesystem::path to_fs(const QString& path) { return std::filesystem::path(path.toStdU16String()); }

QString from_fs(const std::filesystem::path& path) { return QString::fromStdU16String(path.u16string()); }

// Packages listed in RIMWORLD_DLC_METADATA; used when a file declares no known expansions.
QList<CaseInsensitiveStr> dlc_package_ids() {
    QList<CaseInsensitiveStr> ids;
    for (const auto& id : RIMWORLD_PACKAGE_IDS) {
        ids.append(CaseInsensitiveStr(id));
    }
    return ids;
}

std::optional<ModsConfig> parse_mods_config_bytes(std::string_view data, const QString& source) {
    pugi::xml_document doc;
    if (const auto result = doc.load_buffer(data.data(), data.size(), kParseFlags); !result) {
        spdlog::error("Failed to parse ModsConfig.xml at {}: {}", source.toStdString(), result.description());
        return std::nullopt;
    }

    const pugi::xml_node root = doc.document_element();
    const pugi::xml_node version_node = root.child("version");
    const pugi::xml_node active_mods_node = root.child("activeMods");
    if (std::string_view(root.name()) != kRootTag || !version_node || !active_mods_node) {
        spdlog::error("Invalid ModsConfig.xml structure at {}", source.toStdString());
        return std::nullopt;
    }

    // lxml's `text or "1.5"`: only a missing or empty text falls back to the default.
    const QString raw_version = rawText(version_node);
    ModsConfig config;
    config.version = raw_version.isEmpty() ? QString::fromLatin1(kDefaultVersion) : raw_version.trimmed();

    for (const pugi::xml_node li : active_mods_node.children("li")) {
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
        config.knownExpansions = dlc_package_ids();
    }
    return config;
}

struct SnapshotFile {
    QString path;
    std::int64_t mtime_ns = 0;
    QString name;
};

// Snapshots ordered by (mtime, name), newest first. Nanosecond mtimes match the
// legacy ordering; names break ties because they embed the creation time.
QStringList snapshot_files(const QString& snapshots_dir) {
    std::vector<SnapshotFile> found;
    std::error_code ec;
    std::filesystem::directory_iterator it(to_fs(snapshots_dir), ec);
    const std::filesystem::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        const QString name = from_fs(entry.path().filename());
        std::error_code entry_ec;
        if (!QDir::match(QStringLiteral("ModsConfig_*.xml"), name) ||
            !std::filesystem::is_regular_file(entry.path(), entry_ec)) {
            continue;
        }
        const auto mtime = std::filesystem::last_write_time(entry.path(), entry_ec);
        if (entry_ec) {
            continue;
        }
        found.push_back(SnapshotFile{
            .path = from_fs(entry.path()),
            .mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch()).count(),
            .name = name,
        });
    }

    std::ranges::sort(found, [](const SnapshotFile& a, const SnapshotFile& b) {
        return std::tie(a.mtime_ns, a.name) > std::tie(b.mtime_ns, b.name);
    });
    QStringList paths;
    for (const auto& snapshot : found) {
        paths.append(snapshot.path);
    }
    return paths;
}

// Removes the oldest snapshots beyond `max_snapshots` (at least one is always kept),
// never removing `preserved`.
void prune_snapshots(const QString& snapshots_dir, int max_snapshots, const QString& preserved = {}) {
    const QStringList snapshots = get_snapshots(snapshots_dir);
    qsizetype remaining = snapshots.size() - std::max(1, max_snapshots);
    for (auto it = snapshots.crbegin(); it != snapshots.crend() && remaining > 0; ++it) {
        if (!preserved.isEmpty() && QDir::cleanPath(*it) == QDir::cleanPath(preserved)) {
            continue;
        }
        --remaining;
        if (!QFile::remove(*it)) {
            spdlog::warn("Failed to remove old snapshot {}", it->toStdString());
        }
    }
}

// UTC timestamp with microseconds, e.g. 20260922_100000_123456.
std::string snapshot_timestamp() {
    using namespace std::chrono;
    const auto micros = duration_cast<microseconds>(system_clock::now().time_since_epoch());
    const auto seconds = floor<std::chrono::seconds>(micros);
    const auto fraction = (micros - seconds).count();
    return std::format("{:%Y%m%d_%H%M%S}_{:06}", sys_seconds{seconds}, fraction);
}

// Replaces the children named `name` with one `li` per item.
void replace_list_items(pugi::xml_node root, const char* name, const QList<CaseInsensitiveStr>& items) {
    pugi::xml_node node = root.child(name);
    if (!node) {
        node = root.append_child(name);
    }
    for (pugi::xml_node li = node.child("li"); li;) {
        const pugi::xml_node next = li.next_sibling("li");
        node.remove_child(li);
        li = next;
    }
    for (const auto& item : items) {
        node.append_child("li").text().set(item.str().toUtf8().constData());
    }
}

}  // namespace

std::optional<ModsConfig> parse_mods_config(const QString& path) {
    auto data = read_file_bytes(path);
    if (!data) {
        spdlog::warn("Failed to read ModsConfig.xml at {}: {}", path.toStdString(),
                     data.error().message.toStdString());
        return std::nullopt;
    }
    return parse_mods_config_bytes(*data, path);
}

QStringList get_snapshots(const QString& snapshots_dir) {
    if (!QFileInfo::exists(snapshots_dir)) {
        return {};
    }
    return snapshot_files(snapshots_dir);
}

Result<std::optional<QString>> create_snapshot(const QString& config_path, const QString& snapshots_dir,
                                               int max_snapshots) {
    if (!QFileInfo(config_path).isFile()) {
        return std::optional<QString>{};
    }
    auto config_data = read_file_bytes(config_path);
    if (!config_data) {
        spdlog::warn("Failed to read {} for snapshot: {}", config_path.toStdString(),
                     config_data.error().message.toStdString());
        return std::optional<QString>{};
    }

    const QStringList snapshots = get_snapshots(snapshots_dir);
    if (!snapshots.isEmpty()) {
        if (auto newest = read_file_bytes(snapshots.first()); newest && *newest == *config_data) {
            prune_snapshots(snapshots_dir, max_snapshots);
            return std::optional<QString>(snapshots.first());
        }
    }

    const QString timestamp = QString::fromStdString(snapshot_timestamp());
    QString snapshot_path = QDir(snapshots_dir).filePath(QStringLiteral("ModsConfig_%1.xml").arg(timestamp));
    for (int counter = 1; QFileInfo::exists(snapshot_path); ++counter) {
        snapshot_path = QDir(snapshots_dir)
                            .filePath(QStringLiteral("ModsConfig_%1_%2.xml")
                                          .arg(timestamp)
                                          .arg(counter, 3, 10, QLatin1Char('0')));
    }

    if (auto written = write_file_atomically(snapshot_path, *config_data); !written) {
        return std::unexpected(written.error());
    }
    prune_snapshots(snapshots_dir, max_snapshots);
    spdlog::info("Snapshot created at {}", snapshot_path.toStdString());
    return std::optional<QString>(snapshot_path);
}

Result<void> restore_snapshot(const QString& snapshot_path, const QString& target_path,
                              const QString& snapshots_dir, int max_snapshots) {
    auto snapshot_data = read_file_bytes(snapshot_path);
    if (!snapshot_data) {
        return std::unexpected(Error{
            QStringLiteral("Failed to read snapshot %1: %2").arg(snapshot_path, snapshot_data.error().message)});
    }
    if (!parse_mods_config_bytes(*snapshot_data, snapshot_path)) {
        return std::unexpected(Error{QStringLiteral("Snapshot is invalid or corrupted: %1").arg(snapshot_path)});
    }

    if (QFileInfo::exists(target_path)) {
        const int limit = std::max(1, max_snapshots);
        if (auto kept = create_snapshot(target_path, snapshots_dir, limit + 1); !kept) {
            return std::unexpected(kept.error());
        }
        prune_snapshots(snapshots_dir, limit, snapshot_path);
    }
    if (auto written = write_file_atomically(target_path, *snapshot_data); !written) {
        return std::unexpected(written.error());
    }
    spdlog::info("Restored snapshot {} to {}", snapshot_path.toStdString(), target_path.toStdString());
    return {};
}

Result<void> write_mods_config(const QString& path, const ModsConfig& data, const QString& snapshots_dir,
                               int max_snapshots) {
    // An existing ModsConfigData document is updated in place so unknown XML survives;
    // anything else starts a fresh document.
    pugi::xml_document doc;
    bool reusable = false;
    if (auto existing = read_file_bytes(path); existing) {
        reusable = doc.load_buffer(existing->data(), existing->size(), kParseFlags) &&
                   std::string_view(doc.document_element().name()) == kRootTag;
    }
    if (!reusable) {
        doc.reset();
        doc.append_child(kRootTag);
    }
    pugi::xml_node root = doc.document_element();

    pugi::xml_node version_node = root.child("version");
    if (!version_node) {
        version_node = root.append_child("version");
    }
    version_node.text().set(data.version.toUtf8().constData());
    replace_list_items(root, "activeMods", data.activeMods);
    replace_list_items(root, "knownExpansions", data.knownExpansions);

    pugi::xml_node declaration = doc.prepend_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "utf-8";

    std::ostringstream out;
    doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);

    if (QFileInfo::exists(path)) {
        if (auto kept = create_snapshot(path, snapshots_dir, max_snapshots); !kept) {
            return std::unexpected(kept.error());
        }
    }
    if (auto written = write_file_atomically(path, out.str()); !written) {
        return std::unexpected(written.error());
    }
    spdlog::info("ModsConfig.xml written to {}", path.toStdString());
    return {};
}

}  // namespace px::modrim
