#pragma once

#include <optional>

#include <QHash>
#include <QLatin1StringView>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace px::core::organizer {

inline constexpr qint64 ROOT_ID = 1;
inline constexpr int MAX_DEPTH = 3;

// Names match the SQL values stored in the rules table's CHECK constraints.
enum class RuleField { PackageId, Name, Author };
enum class RuleOp { Prefix, Contains, Equals };

inline QLatin1StringView rule_field_name(RuleField field) {
    switch (field) {
        case RuleField::PackageId: return QLatin1StringView("package_id");
        case RuleField::Name: return QLatin1StringView("name");
        case RuleField::Author: return QLatin1StringView("author");
    }
    return {};
}

inline QLatin1StringView rule_op_name(RuleOp op) {
    switch (op) {
        case RuleOp::Prefix: return QLatin1StringView("prefix");
        case RuleOp::Contains: return QLatin1StringView("contains");
        case RuleOp::Equals: return QLatin1StringView("equals");
    }
    return {};
}

inline std::optional<RuleField> parse_rule_field(const QString& name) {
    if (name == QLatin1StringView("package_id")) return RuleField::PackageId;
    if (name == QLatin1StringView("name")) return RuleField::Name;
    if (name == QLatin1StringView("author")) return RuleField::Author;
    return std::nullopt;
}

inline std::optional<RuleOp> parse_rule_op(const QString& name) {
    if (name == QLatin1StringView("prefix")) return RuleOp::Prefix;
    if (name == QLatin1StringView("contains")) return RuleOp::Contains;
    if (name == QLatin1StringView("equals")) return RuleOp::Equals;
    return std::nullopt;
}

struct Folder {
    qint64 id = 0;
    std::optional<qint64> parent_id;  // empty only for ROOT_ID
    QString name;
    bool collapsed = false;

    friend bool operator==(const Folder&, const Folder&) = default;
};

struct Tag {
    qint64 id = 0;
    QString name;
    QString color;

    friend bool operator==(const Tag&, const Tag&) = default;
};

struct RuleSpec {
    RuleField field = RuleField::Name;
    RuleOp op = RuleOp::Equals;
    QString pattern;
    qint64 folder_id = 0;

    friend bool operator==(const RuleSpec&, const RuleSpec&) = default;
};

struct Rule {
    qint64 id = 0;
    qint64 position = 0;
    RuleField field = RuleField::Name;
    RuleOp op = RuleOp::Equals;
    QString pattern;
    qint64 folder_id = 0;

    friend bool operator==(const Rule&, const Rule&) = default;
};

// Everything the organizer keeps in memory, as loaded from organizer.db.
// Package ids are lowercase keys.
struct OrganizerState {
    QHash<qint64, Folder> folders;
    QHash<QString, qint64> placements;
    QHash<qint64, Tag> tags;
    QHash<QString, QSet<qint64>> mod_tags;
    QList<Rule> rules;

    friend bool operator==(const OrganizerState&, const OrganizerState&) = default;
};

// Lowercased, trimmed, de-duplicated package ids in first-seen order; blanks are dropped.
inline QStringList clean_package_ids(const QStringList& package_ids) {
    QStringList cleaned;
    QSet<QString> seen;
    for (const QString& raw : package_ids) {
        const QString pid = raw.trimmed().toLower();
        if (!pid.isEmpty() && !seen.contains(pid)) {
            seen.insert(pid);
            cleaned.append(pid);
        }
    }
    return cleaned;
}

}  // namespace px::core::organizer
