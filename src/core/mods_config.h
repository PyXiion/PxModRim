#pragma once

#include <optional>

#include <QString>
#include <QStringList>

#include "core/error.h"
#include "core/models/metadata/structures.h"

namespace px::core {

// Parses ModsConfig.xml. nullopt when the file is unreadable, malformed, or not a ModsConfig document.
std::optional<ModsConfig> parse_mods_config(const QString& path);

// Snapshot files in `snapshots_dir`, newest first.
QStringList get_snapshots(const QString& snapshots_dir);

// Keeps a copy of `config_path` in `snapshots_dir`. Yields the snapshot that holds the
// current contents (an existing identical one, or a new one), or nullopt when `config_path`
// is not a readable file.
Result<std::optional<QString>> create_snapshot(const QString& config_path, const QString& snapshots_dir,
                                               int max_snapshots = 10);

// Replaces `target_path` with a valid snapshot, keeping the replaced file as a snapshot.
Result<void> restore_snapshot(const QString& snapshot_path, const QString& target_path,
                              const QString& snapshots_dir, int max_snapshots = 10);

// Writes `data` into `path`, keeping unknown XML in an existing ModsConfigData document.
// The previous file is kept as a snapshot first.
Result<void> write_mods_config(const QString& path, const ModsConfig& data, const QString& snapshots_dir,
                               int max_snapshots = 10);

}  // namespace px::core
