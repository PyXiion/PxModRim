#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

#include "core/context.hpp"
#include "core/models/view/diagnostics.hpp"
namespace px::modrim {
inline const QString kImgWidthToken = QStringLiteral("__IMG_WIDTH__");
QString bbcodeImages(const QString& text);
QString restoreEntities(QString text);
QString bbcodeMarkup(QString text);
QString autolink(const QString& text);
QString descriptionToHtml(
    const QString& text,
    const QString& descriptionFormat = QStringLiteral("bbcode"));
QString formatAge(double mtime, std::optional<double> now = std::nullopt);

// The QML `info` map of one mod, split by what invalidates it: the mod's
// metadata and the game configuration, the active list, and the diagnostics.
struct ModInfo {
  // Rendered description, exposed as its own QML property.
  QString description;
  // Incompatible mods present in the load order as {name, uuid}, sorted by
  // name, so active-state updates only need the active set.
  QVariantList conflictingMods;
  // Name, author, provider, version, details, url, dependency topology
  // (`needs` and `neededBy` with resolved UUIDs), and status obsolete/valid.
  QVariantMap info;
};

// Everything only the mod's metadata and the game configuration determine.
ModInfo buildModInfo(CoreContext& ctx, const ListedMod& mod);

// Fills status active/position/total, every `needs` entry's state and the
// active conflicting mod names for `activeUuids`.
void updateActiveState(const QStringList& activeUuids, const QString& uuid,
                       ModInfo& data);

// Fills status level/title/issues.
void updateIssues(const QList<view::ModIssueView>& issues, QVariantMap& info);
}  // namespace px::modrim
