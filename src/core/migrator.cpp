#include "core/migrator.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

#include <spdlog/spdlog.h>

#include "core/file_io.hpp"

namespace px::modrim {

namespace {

Result<void> execute(QSqlDatabase& db, const QString& sql) {
  QSqlQuery query(db);
  if (!query.exec(sql)) {
    return std::unexpected(Error{query.lastError().text()});
  }
  return {};
}

// QSqlQuery runs one statement per exec, so a schema script is split first.
// Semicolons inside string literals, quoted identifiers, and comments do not
// split. CREATE TRIGGER bodies are not recognised: their inner semicolons split
// and fail.
QStringList splitStatements(const QString& script) {
  QStringList statements;
  QString current;
  QChar quote;
  bool inLineComment = false;
  bool inBlockComment = false;

  const auto flush = [&statements](QString& text) {
    if (const QString statement = text.trimmed(); !statement.isEmpty()) {
      statements.append(statement);
    }
    text.clear();
  };

  for (qsizetype i = 0; i < script.size(); ++i) {
    const QChar c = script[i];
    const QChar next = i + 1 < script.size() ? script[i + 1] : QChar();
    current.append(c);
    if (inLineComment) {
      inLineComment = c != u'\n';
    } else if (inBlockComment) {
      if (c == u'*' && next == u'/') {
        inBlockComment = false;
        current.append(next);
        ++i;
      }
    } else if (!quote.isNull()) {
      if (c == quote) {
        if (next == quote) {
          current.append(next);
          ++i;
        } else {
          quote = QChar();
        }
      }
    } else if (c == u'-' && next == u'-') {
      inLineComment = true;
    } else if (c == u'/' && next == u'*') {
      inBlockComment = true;
      current.append(next);
      ++i;
    } else if (c == u'\'' || c == u'"' || c == u'`') {
      quote = c;
    } else if (c == u';') {
      flush(current);
    }
  }
  flush(current);
  return statements;
}

Result<void> executeScript(QSqlDatabase& db, const QString& script) {
  for (const auto& statement : splitStatements(script)) {
    if (auto applied = execute(db, statement); !applied) {
      return applied;
    }
  }
  return {};
}

Result<int> userVersion(QSqlDatabase& db) {
  QSqlQuery query(db);
  if (!query.exec(QStringLiteral("PRAGMA user_version"))) {
    return std::unexpected(Error{query.lastError().text()});
  }
  return query.next() ? query.value(0).toInt() : 0;
}

// `path` with its last suffix replaced, as Python's Path.with_suffix does.
QString withSuffix(const QString& path, const QString& suffix) {
  const QFileInfo info(path);
  const QString name = info.fileName();
  const qsizetype dot = name.lastIndexOf(u'.');
  const QString stem = dot > 0 ? name.left(dot) : name;
  return QDir(info.absolutePath()).filePath(stem + suffix);
}

}  // namespace

Result<void> ensureSchema(QSqlDatabase& db, const QString& schemaSql,
                          int schemaVersion, const MigrationSteps& steps,
                          const std::optional<QString>& backupPath,
                          const QString& label) {
  auto currentVersion = userVersion(db);
  if (!currentVersion) {
    return std::unexpected(currentVersion.error());
  }
  const int current = *currentVersion;
  if (current >= schemaVersion) {
    return {};
  }

  const auto stamp = [&db](int version) {
    return execute(db, QStringLiteral("PRAGMA user_version = %1").arg(version));
  };

  if (current == 0) {
    spdlog::info("Initializing {} (version {})", label.toStdString(),
                 schemaVersion);
    if (auto initialized = executeScript(db, schemaSql); !initialized) {
      return initialized;
    }
    return stamp(schemaVersion);
  }

  spdlog::info("Migrating {} {} -> {}", label.toStdString(), current,
               schemaVersion);
  if (backupPath) {
    // Fold the WAL into the main file so the copy below is complete.
    if (auto checkpointed =
            execute(db, QStringLiteral("PRAGMA wal_checkpoint(FULL)"));
        !checkpointed) {
      return checkpointed;
    }
    const QString backup = withSuffix(
        *backupPath,
        QStringLiteral(".db.bak.%1").arg(QDateTime::currentSecsSinceEpoch()));
    if (auto copied = copyFileReplacing(*backupPath, backup); !copied) {
      return copied;
    }
  }

  for (int target = current + 1; target <= schemaVersion; ++target) {
    if (!steps.contains(target)) {
      return std::unexpected(
          Error{QStringLiteral("Missing migration step for target version %1")
                    .arg(target)});
    }
  }
  for (int target = current + 1; target <= schemaVersion; ++target) {
    if (auto applied = steps.at(target)(); !applied) {
      return applied;
    }
    if (auto stamped = stamp(target); !stamped) {
      return stamped;
    }
  }
  return {};
}

}  // namespace px::modrim
