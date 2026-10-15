#pragma once

#include <functional>
#include <map>
#include <optional>

#include <QSqlDatabase>
#include <QString>

#include "core/error.h"

namespace px::core {

// Applies one schema step, leaving the database at the step's version.
using MigrationStep = std::function<Result<void>()>;

// Keyed by target schema version.
using MigrationSteps = std::map<int, MigrationStep>;

// Brings the `PRAGMA user_version` of `db` up to `schema_version`. A fresh database
// (version 0) gets `schema_sql`; an older one runs the steps in order, and a file
// copy of `backup_path` is taken first. Runs synchronously: call it on the thread
// that owns `db`.
Result<void> ensure_schema(QSqlDatabase& db, const QString& schema_sql, int schema_version,
                           const MigrationSteps& steps, const std::optional<QString>& backup_path = std::nullopt,
                           const QString& label = QStringLiteral("schema"));

}  // namespace px::core
