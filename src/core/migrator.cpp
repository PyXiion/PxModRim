#include "core/migrator.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <spdlog/spdlog.h>

#include "core/file_io.h"

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
// Semicolons inside string literals, quoted identifiers, and comments do not split.
// CREATE TRIGGER bodies are not recognised: their inner semicolons split and fail.
QStringList split_statements(const QString& script) {
    QStringList statements;
    QString current;
    QChar quote;
    bool in_line_comment = false;
    bool in_block_comment = false;

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
        if (in_line_comment) {
            in_line_comment = c != u'\n';
        } else if (in_block_comment) {
            if (c == u'*' && next == u'/') {
                in_block_comment = false;
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
            in_line_comment = true;
        } else if (c == u'/' && next == u'*') {
            in_block_comment = true;
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

Result<void> execute_script(QSqlDatabase& db, const QString& script) {
    for (const auto& statement : split_statements(script)) {
        if (auto applied = execute(db, statement); !applied) {
            return applied;
        }
    }
    return {};
}

Result<int> user_version(QSqlDatabase& db) {
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA user_version"))) {
        return std::unexpected(Error{query.lastError().text()});
    }
    return query.next() ? query.value(0).toInt() : 0;
}

// `path` with its last suffix replaced, as Python's Path.with_suffix does.
QString with_suffix(const QString& path, const QString& suffix) {
    const QFileInfo info(path);
    const QString name = info.fileName();
    const qsizetype dot = name.lastIndexOf(u'.');
    const QString stem = dot > 0 ? name.left(dot) : name;
    return QDir(info.absolutePath()).filePath(stem + suffix);
}

}  // namespace

Result<void> ensure_schema(QSqlDatabase& db, const QString& schema_sql, int schema_version,
                           const MigrationSteps& steps, const std::optional<QString>& backup_path,
                           const QString& label) {
    auto current_version = user_version(db);
    if (!current_version) {
        return std::unexpected(current_version.error());
    }
    const int current = *current_version;
    if (current >= schema_version) {
        return {};
    }

    const auto stamp = [&db](int version) {
        return execute(db, QStringLiteral("PRAGMA user_version = %1").arg(version));
    };

    if (current == 0) {
        spdlog::info("Initializing {} (version {})", label.toStdString(), schema_version);
        if (auto initialized = execute_script(db, schema_sql); !initialized) {
            return initialized;
        }
        return stamp(schema_version);
    }

    spdlog::info("Migrating {} {} -> {}", label.toStdString(), current, schema_version);
    if (backup_path) {
        // Fold the WAL into the main file so the copy below is complete.
        if (auto checkpointed = execute(db, QStringLiteral("PRAGMA wal_checkpoint(FULL)")); !checkpointed) {
            return checkpointed;
        }
        const QString backup = with_suffix(
            *backup_path, QStringLiteral(".db.bak.%1").arg(QDateTime::currentSecsSinceEpoch()));
        if (auto copied = copy_file_replacing(*backup_path, backup); !copied) {
            return copied;
        }
    }

    for (int target = current + 1; target <= schema_version; ++target) {
        if (!steps.contains(target)) {
            return std::unexpected(
                Error{QStringLiteral("Missing migration step for target version %1").arg(target)});
        }
    }
    for (int target = current + 1; target <= schema_version; ++target) {
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
