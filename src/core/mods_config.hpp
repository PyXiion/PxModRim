#pragma once

#include <QString>
#include <QStringList>
#include <optional>

#include "core/error.hpp"
#include "core/models/metadata/structures.hpp"

namespace px::modrim {

// <config folder>/ModsConfig.xml.
QString modsConfigPath(const QString& configFolder);

// Parses ModsConfig.xml. nullopt when the file is unreadable, malformed, or not
// a ModsConfig document.
std::optional<ModsConfig> parseModsConfig(const QString& path);

// Snapshot files in `snapshots_dir`, newest first.
QStringList getSnapshots(const QString& snapshotsDir);

// Keeps a copy of `config_path` in `snapshots_dir`. Yields the snapshot that
// holds the current contents (an existing identical one, or a new one), or
// nullopt when `config_path` is not a readable file.
Result<std::optional<QString>> createSnapshot(const QString& configPath,
                                              const QString& snapshotsDir,
                                              int maxSnapshots = 10);

// Replaces `target_path` with a valid snapshot, keeping the replaced file as a
// snapshot.
Result<void> restoreSnapshot(const QString& snapshotPath,
                             const QString& targetPath,
                             const QString& snapshotsDir,
                             int maxSnapshots = 10);

// Writes `data` into `path`, keeping unknown XML in an existing ModsConfigData
// document. The previous file is kept as a snapshot first.
Result<void> writeModsConfig(const QString& path, const ModsConfig& data,
                             const QString& snapshotsDir,
                             int maxSnapshots = 10);

}  // namespace px::modrim
