#pragma once

#include <optional>

#include <QHash>
#include <QLatin1StringView>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace px::modrim::organizer {

inline constexpr qint64 kRootId = 1;
inline constexpr int kMaxDepth = 3;

// Names match the SQL values stored in the rules table's CHECK constraints.
enum class RuleField { kPackageId, kName, kAuthor };
enum class RuleOp { kPrefix, kContains, kEquals };

inline QLatin1StringView ruleFieldName(RuleField field) {
    switch (field) {
        case RuleField::kPackageId: return QLatin1StringView("package_id");
        case RuleField::kName: return QLatin1StringView("name");
        case RuleField::kAuthor: return QLatin1StringView("author");
    }
    return {};
}

inline QLatin1StringView ruleOpName(RuleOp op) {
    switch (op) {
        case RuleOp::kPrefix: return QLatin1StringView("prefix");
        case RuleOp::kContains: return QLatin1StringView("contains");
        case RuleOp::kEquals: return QLatin1StringView("equals");
    }
    return {};
}

inline std::optional<RuleField> parseRuleField(const QString& name) {
    if (name == QLatin1StringView("package_id")) return RuleField::kPackageId;
    if (name == QLatin1StringView("name")) return RuleField::kName;
    if (name == QLatin1StringView("author")) return RuleField::kAuthor;
    return std::nullopt;
}

inline std::optional<RuleOp> parseRuleOp(const QString& name) {
    if (name == QLatin1StringView("prefix")) return RuleOp::kPrefix;
    if (name == QLatin1StringView("contains")) return RuleOp::kContains;
    if (name == QLatin1StringView("equals")) return RuleOp::kEquals;
    return std::nullopt;
}

struct Folder {
    qint64 id = 0;
    std::optional<qint64> parentId;  // empty only for ROOT_ID
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
    RuleField field = RuleField::kName;
    RuleOp op = RuleOp::kEquals;
    QString pattern;
    qint64 folderId = 0;

    friend bool operator==(const RuleSpec&, const RuleSpec&) = default;
};

struct Rule {
    qint64 id = 0;
    qint64 position = 0;
    RuleField field = RuleField::kName;
    RuleOp op = RuleOp::kEquals;
    QString pattern;
    qint64 folderId = 0;

    friend bool operator==(const Rule&, const Rule&) = default;
};

// Everything the organizer keeps in memory, as loaded from organizer.db.
// Package ids are lowercase keys.
struct OrganizerState {
    QHash<qint64, Folder> folders;
    QHash<QString, qint64> placements;
    QHash<qint64, Tag> tags;
    QHash<QString, QSet<qint64>> modTags;
    QList<Rule> rules;

    friend bool operator==(const OrganizerState&, const OrganizerState&) = default;
};

// Lowercased, trimmed, de-duplicated package ids in first-seen order; blanks are dropped.
inline QStringList cleanPackageIds(const QStringList& packageIds) {
    QStringList cleaned;
    QSet<QString> seen;
    for (const QString& raw : packageIds) {
        const QString pid = raw.trimmed().toLower();
        if (!pid.isEmpty() && !seen.contains(pid)) {
            seen.insert(pid);
            cleaned.append(pid);
        }
    }
    return cleaned;
}

}  // namespace px::modrim::organizer
