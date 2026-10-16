#pragma once

#include <string>
#include <string_view>

#include <QString>

#include "core/error.h"

namespace px::modrim {

Result<std::string> read_file_bytes(const QString& path);

// Writes `data` to a temporary file beside `path`, syncs it, then renames it
// over `path`, so readers see either the old or the new contents. Missing
// parent directories are created.
Result<void> write_file_atomically(const QString& path, std::string_view data);

// Copies `source` to `target`, replacing `target` if it exists.
Result<void> copy_file_replacing(const QString& source, const QString& target);

}  // namespace px::modrim
