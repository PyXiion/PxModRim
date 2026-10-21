#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <optional>
#include <vector>

#include "core/models/metadata/structures.hpp"
#include "core/organizer/models.hpp"

namespace px::modrim::organizer {

// Discovered mods keyed by uuid.
using ModsByUuid = QHash<QString, ListedModPtr>;

// Where a mod's folder came from: a manual placement, an auto-folder rule, or
// nowhere (root).
enum class Placement { kManual, kRule, kNone };
enum class CheckState { kOn, kOff, kPartial };
enum class StatusFilter { kAll, kActive, kInactive };

struct Resolution {
  qint64 folderId = kRootId;
  Placement placement = Placement::kNone;
};

struct TreeQuery {
  QString text;
  QSet<qint64> tagIds;
  StatusFilter status = StatusFilter::kAll;
  QSet<QString> providerIds;

  bool isEmpty() const;
};

// A sidebar preset (status, provider, or tag) and the mods it selects.
struct TreeFilter {
  QString key;
  QString label;
  int count = 0;
  StatusFilter status = StatusFilter::kAll;
  std::optional<QString> providerId;
  std::optional<qint64> tagId;

  TreeQuery query(const QString& text = {}) const;
};

struct ModLeaf {
  QString uuid;
  std::optional<QString> packageId;
  QString name;
  QString author;
  bool enabled = false;
  QSet<qint64> tagIds;
  Placement placement = Placement::kNone;
};

struct FolderNode {
  Folder folder;
  int depth = 0;
  // std::vector: QList cannot hold its own incomplete element type.
  std::vector<FolderNode> children;
  QList<ModLeaf> mods;
  int total = 0;
  int enabled = 0;
  CheckState check = CheckState::kOff;
  int visibleTotal = 0;
  int ownTotal = 0;
  int ownEnabled = 0;
  bool hasRules = false;

  // Check state of the folder's direct mods only (the root's Ungrouped).
  CheckState ownCheck() const;
  // This node followed by all descendants, in pre-order.
  QList<const FolderNode*> walk() const;
  QList<ModLeaf> allMods() const;
};

// First-match counts for a draft rule list, and the ungrouped mods it would
// assign.
struct RulePreview {
  QList<int> counts;
  int assignable = 0;
};

// Lowercased package id for About.xml mods; nullopt for mods without one.
std::optional<QString> modPackageId(const ListedMod& mod);

Resolution folderFor(const OrganizerState& state,
                     const std::optional<QString>& packageId,
                     const QString& name, const QString& author);

RulePreview previewRuleMatches(const OrganizerState& state,
                               const ModsByUuid& mods,
                               const QList<RuleSpec>& specs);

QList<TreeFilter> treeFilters(const ModsByUuid& mods,
                              const QSet<QString>& active,
                              const OrganizerState& state);

FolderNode buildTree(const OrganizerState& state, const ModsByUuid& mods,
                     const QSet<QString>& active,
                     const std::optional<TreeQuery>& query = std::nullopt);

}  // namespace px::modrim::organizer
