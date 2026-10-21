#include "core/organizer/service.hpp"

#include <QRegularExpression>
#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/context.hpp"
#include "core/organizer/defaults.hpp"

namespace px::modrim::organizer {
namespace {

const FolderNode* findNode(const FolderNode& root, qint64 folderId) {
  for (const FolderNode* node : root.walk()) {
    if (node->folder.id == folderId) {
      return node;
    }
  }
  return nullptr;
}

QStringList uuidsOf(const QList<ModLeaf>& leaves) {
  QStringList uuids;
  for (const ModLeaf& leaf : leaves) {
    uuids.append(leaf.uuid);
  }
  return uuids;
}

Result<QString> cleanName(const QString& name, const QString& what) {
  const QString cleaned = name.trimmed();
  if (cleaned.isEmpty()) {
    return std::unexpected(
        Error{QStringLiteral("%1 name cannot be empty.").arg(what)});
  }
  return cleaned;
}

}  // namespace

OrganizerService::OrganizerService(CoreContext& ctx, OrganizerDb& db)
    : OrganizerService(
          [&ctx]() -> const QHash<QString, ListedModPtr>& {
            return ctx.allMods();
          },
          [&ctx]() -> const QStringList& { return ctx.activeUuids(); }, db) {}

OrganizerService::OrganizerService(AllModsFn allMods, ActiveUuidsFn activeUuids,
                                   OrganizerDb& db)
    : Plugin(QStringLiteral("organizer")),
      m_allModsFn(std::move(allMods)),
      m_activeUuidsFn(std::move(activeUuids)),
      m_db(db) {}

const QHash<QString, ListedModPtr>& OrganizerService::allMods() const {
  return m_allModsFn();
}

const QStringList& OrganizerService::activeUuids() const {
  return m_activeUuidsFn();
}

QCoro::Task<void> OrganizerService::init() {
  if (auto loaded = co_await commit(); !loaded) {
    // Plugin::init cannot report failure, so the service stays not ready and
    // says why.
    spdlog::error("Cannot load organizer state: {}",
                  loaded.error().message.toStdString());
  }
}

QCoro::Task<void> OrganizerService::init(CoreContext&) { co_await init(); }

QCoro::Task<void> OrganizerService::shutdown() { co_await m_db.close(); }

bool OrganizerService::ready() const noexcept { return m_state.has_value(); }

const OrganizerState& OrganizerService::state() const {
  Q_ASSERT(m_state.has_value());
  return *m_state;
}

QCoro::Task<Result<void>> OrganizerService::commit() {
  auto loaded = co_await m_db.load();
  if (!loaded) {
    co_return std::unexpected(loaded.error());
  }
  m_state = std::move(*loaded);
  changed.notify({});
  co_return {};
}

// ── Validation helpers ───────────────────────────────────────────────────────

Result<Folder> OrganizerService::findFolder(qint64 folderId) const {
  const auto& folders = state().folders;
  const auto it = folders.constFind(folderId);
  if (it == folders.cend()) {
    return std::unexpected(
        Error{QStringLiteral("That folder no longer exists.")});
  }
  return it.value();
}

Result<Folder> OrganizerService::findNonRoot(qint64 folderId) const {
  if (folderId == kRootId) {
    return std::unexpected(
        Error{QStringLiteral("The root folder cannot be changed.")});
  }
  return findFolder(folderId);
}

Result<Tag> OrganizerService::findTag(qint64 tagId) const {
  const auto& tags = state().tags;
  const auto it = tags.constFind(tagId);
  if (it == tags.cend()) {
    return std::unexpected(Error{QStringLiteral("That tag no longer exists.")});
  }
  return it.value();
}

int OrganizerService::depth(qint64 folderId) const {
  const auto& folders = state().folders;
  int count = 0;
  auto current = folders.constFind(folderId);
  while (current != folders.cend() && current->parentId && count <= kMaxDepth) {
    ++count;
    current = folders.constFind(*current->parentId);
  }
  return count;
}

QSet<qint64> OrganizerService::subtreeIds(qint64 folderId) const {
  QSet<qint64> ids{folderId};
  QList<qint64> pending{folderId};
  while (!pending.isEmpty()) {
    const qint64 current = pending.takeLast();
    for (const Folder& child : state().folders) {
      if (child.parentId == current && !ids.contains(child.id)) {
        ids.insert(child.id);
        pending.append(child.id);
      }
    }
  }
  return ids;
}

int OrganizerService::height(qint64 folderId, int limit) const {
  if (limit == 1) {
    return 1;
  }
  int tallest = 0;
  for (const Folder& child : state().folders) {
    if (child.parentId == folderId) {
      tallest = std::max(tallest, height(child.id, limit - 1));
    }
  }
  return 1 + tallest;
}

Result<void> OrganizerService::checkDepth(qint64 parentId,
                                          int treeHeight) const {
  if (depth(parentId) + treeHeight > kMaxDepth) {
    return std::unexpected(
        Error{QStringLiteral("Folders can be nested at most %1 levels deep.")
                  .arg(kMaxDepth)});
  }
  return {};
}

Result<void> OrganizerService::checkUniqueFolderName(
    qint64 parentId, const QString& name, std::optional<qint64> exclude) const {
  const QString key = name.toCaseFolded();
  for (const Folder& sibling : state().folders) {
    if (sibling.parentId != parentId || sibling.id == exclude) {
      continue;
    }
    if (sibling.name.toCaseFolded() == key) {
      return std::unexpected(
          Error{QStringLiteral("A folder named \"%1\" already exists here.")
                    .arg(name)});
    }
  }
  return {};
}

Result<QString> OrganizerService::checkTag(
    const QString& name, const QString& color,
    std::optional<qint64> exclude) const {
  auto cleaned = cleanName(name, QStringLiteral("Tag"));
  if (!cleaned) {
    return std::unexpected(cleaned.error());
  }
  const QString key = cleaned->toCaseFolded();
  for (const Tag& tag : state().tags) {
    if (exclude && tag.id == *exclude) {
      continue;
    }
    if (tag.name.toCaseFolded() == key) {
      return std::unexpected(Error{
          QStringLiteral("A tag named \"%1\" already exists.").arg(*cleaned)});
    }
  }
  static const QRegularExpression colorPattern(
      QStringLiteral(R"(\A#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})\z)"));
  if (!colorPattern.match(color).hasMatch()) {
    return std::unexpected(
        Error{QStringLiteral("\"%1\" is not a valid color (use #RRGGBB).")
                  .arg(color)});
  }
  return *cleaned;
}

Result<QString> OrganizerService::validateNewFolder(const QString& name,
                                                    qint64 parentId) const {
  auto cleaned = cleanName(name, QStringLiteral("Folder"));
  if (!cleaned) {
    return std::unexpected(cleaned.error());
  }
  if (auto parent = findFolder(parentId); !parent) {
    return std::unexpected(parent.error());
  }
  if (auto fits = checkDepth(parentId, 1); !fits) {
    return std::unexpected(fits.error());
  }
  if (auto unique = checkUniqueFolderName(parentId, *cleaned); !unique) {
    return std::unexpected(unique.error());
  }
  return *cleaned;
}

QSet<QString> OrganizerService::activeSet() const {
  const QStringList& active = activeUuids();
  return QSet<QString>(active.cbegin(), active.cend());
}

// ── Folders ──────────────────────────────────────────────────────────────────

QCoro::Task<Result<Folder>> OrganizerService::createFolder(QString name,
                                                           qint64 parentId) {
  return createFolderFrom(std::move(name), {}, parentId);
}

QCoro::Task<Result<Folder>> OrganizerService::createFolderFrom(
    QString name, QStringList packageIds, qint64 parentId) {
  auto cleaned = validateNewFolder(name, parentId);
  if (!cleaned) {
    co_return std::unexpected(cleaned.error());
  }
  auto created =
      co_await m_db.createFolderWith(parentId, *cleaned, std::move(packageIds));
  if (!created) {
    co_return std::unexpected(created.error());
  }
  if (auto committed = co_await commit(); !committed) {
    co_return std::unexpected(committed.error());
  }
  co_return *created;
}

QCoro::Task<Result<void>> OrganizerService::renameFolder(qint64 folderId,
                                                         QString name) {
  auto existing = findNonRoot(folderId);
  if (!existing) {
    co_return std::unexpected(existing.error());
  }
  auto cleaned = cleanName(name, QStringLiteral("Folder"));
  if (!cleaned) {
    co_return std::unexpected(cleaned.error());
  }
  if (*cleaned == existing->name) {
    co_return {};
  }
  const qint64 parentId = existing->parentId.value_or(kRootId);
  if (auto unique = checkUniqueFolderName(parentId, *cleaned, folderId);
      !unique) {
    co_return std::unexpected(unique.error());
  }
  if (auto renamed = co_await m_db.renameFolder(folderId, *cleaned); !renamed) {
    co_return std::unexpected(renamed.error());
  }
  co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::moveFolder(qint64 folderId,
                                                       qint64 parentId) {
  auto folder = findNonRoot(folderId);
  if (!folder) {
    co_return std::unexpected(folder.error());
  }
  if (auto target = findFolder(parentId); !target) {
    co_return std::unexpected(target.error());
  }
  if (folder->parentId == parentId) {
    co_return {};
  }
  if (subtreeIds(folderId).contains(parentId)) {
    co_return std::unexpected(
        Error{QStringLiteral("A folder cannot be moved into itself.")});
  }
  // The whole subtree moves, so its height counts against the depth limit.
  if (auto fits = checkDepth(parentId, height(folderId)); !fits) {
    co_return std::unexpected(fits.error());
  }
  if (auto unique = checkUniqueFolderName(parentId, folder->name, folderId);
      !unique) {
    co_return std::unexpected(unique.error());
  }
  if (auto moved = co_await m_db.setParent(folderId, parentId); !moved) {
    co_return std::unexpected(moved.error());
  }
  co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::setCollapsed(qint64 folderId,
                                                         bool collapsed) {
  auto folder = findFolder(folderId);
  if (!folder) {
    co_return std::unexpected(folder.error());
  }
  if (folder->collapsed == collapsed) {
    co_return {};
  }
  if (auto updated = co_await m_db.setCollapsed(folderId, collapsed);
      !updated) {
    co_return std::unexpected(updated.error());
  }
  co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::setAllCollapsed(bool collapsed) {
  const bool unchanged =
      std::all_of(state().folders.cbegin(), state().folders.cend(),
                  [collapsed](const Folder& folder) {
                    return folder.collapsed == collapsed;
                  });
  if (unchanged) {
    co_return {};
  }
  if (auto updated = co_await m_db.setAllCollapsed(collapsed); !updated) {
    co_return std::unexpected(updated.error());
  }
  co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::deleteFolder(qint64 folderId) {
  if (auto folder = findNonRoot(folderId); !folder) {
    co_return std::unexpected(folder.error());
  }
  if (auto deleted = co_await m_db.deleteFolder(folderId); !deleted) {
    co_return std::unexpected(deleted.error());
  }
  co_return co_await commit();
}

// ── Placement ────────────────────────────────────────────────────────────────

QCoro::Task<Result<void>> OrganizerService::place(QStringList packageIds,
                                                  qint64 folderId) {
  if (auto folder = findFolder(folderId); !folder) {
    co_return std::unexpected(folder.error());
  }
  if (cleanPackageIds(packageIds).isEmpty()) {
    co_return {};
  }
  if (auto placed = co_await m_db.place(std::move(packageIds), folderId);
      !placed) {
    co_return std::unexpected(placed.error());
  }
  co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::ungroup(QStringList packageIds) {
  co_return co_await place(std::move(packageIds), kRootId);
}

QCoro::Task<Result<void>> OrganizerService::resetToRules(
    QStringList packageIds) {
  if (cleanPackageIds(packageIds).isEmpty()) {
    co_return {};
  }
  if (auto unplaced = co_await m_db.unplace(std::move(packageIds)); !unplaced) {
    co_return std::unexpected(unplaced.error());
  }
  co_return co_await commit();
}

// ── Tags ─────────────────────────────────────────────────────────────────────

QCoro::Task<Result<Tag>> OrganizerService::createTag(QString name,
                                                     QString color) {
  auto cleaned = checkTag(name, color);
  if (!cleaned) {
    co_return std::unexpected(cleaned.error());
  }
  auto created = co_await m_db.createTag(*cleaned, color);
  if (!created) {
    co_return std::unexpected(created.error());
  }
  if (auto committed = co_await commit(); !committed) {
    co_return std::unexpected(committed.error());
  }
  co_return *created;
}

QCoro::Task<Result<void>> OrganizerService::updateTag(qint64 tagId,
                                                      QString name,
                                                      QString color) {
  if (auto tag = findTag(tagId); !tag) {
    co_return std::unexpected(tag.error());
  }
  auto cleaned = checkTag(name, color, tagId);
  if (!cleaned) {
    co_return std::unexpected(cleaned.error());
  }
  if (auto updated = co_await m_db.updateTag(tagId, *cleaned, color);
      !updated) {
    co_return std::unexpected(updated.error());
  }
  co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::deleteTag(qint64 tagId) {
  if (auto tag = findTag(tagId); !tag) {
    co_return std::unexpected(tag.error());
  }
  if (auto deleted = co_await m_db.deleteTag(tagId); !deleted) {
    co_return std::unexpected(deleted.error());
  }
  co_return co_await commit();
}

QCoro::Task<Result<void>> OrganizerService::setModTags(QStringList packageIds,
                                                       QSet<qint64> add,
                                                       QSet<qint64> remove) {
  if (add.intersects(remove)) {
    co_return std::unexpected(
        Error{QStringLiteral("A tag cannot be added and removed at once.")});
  }
  for (qint64 tagId : add | remove) {
    if (auto tag = findTag(tagId); !tag) {
      co_return std::unexpected(tag.error());
    }
  }
  if (cleanPackageIds(packageIds).isEmpty() ||
      (add.isEmpty() && remove.isEmpty())) {
    co_return {};
  }
  if (auto updated =
          co_await m_db.setModTags(std::move(packageIds), add, remove);
      !updated) {
    co_return std::unexpected(updated.error());
  }
  co_return co_await commit();
}

// ── Rules ────────────────────────────────────────────────────────────────────

QCoro::Task<Result<void>> OrganizerService::setRules(QList<RuleSpec> specs) {
  for (RuleSpec& spec : specs) {
    spec.pattern = spec.pattern.trimmed();
    if (spec.pattern.isEmpty()) {
      co_return std::unexpected(
          Error{QStringLiteral("Rule pattern cannot be empty.")});
    }
    if (spec.folderId == kRootId) {
      co_return std::unexpected(
          Error{QStringLiteral("Rules must target a folder, not the root.")});
    }
    if (auto target = findFolder(spec.folderId); !target) {
      co_return std::unexpected(target.error());
    }
  }
  if (auto replaced = co_await m_db.replaceRules(specs); !replaced) {
    co_return std::unexpected(replaced.error());
  }
  co_return co_await commit();
}

QCoro::Task<Result<int>> OrganizerService::addStandardRules() {
  // (field, op, casefolded pattern) identifies a rule regardless of folder or
  // position.
  using RuleKey = std::tuple<int, int, QString>;
  std::set<RuleKey> present;
  for (const RuleSpec& rule : state().rules) {
    present.emplace(static_cast<int>(rule.field), static_cast<int>(rule.op),
                    rule.pattern.toCaseFolded());
  }

  QSet<QString> knownFolders;
  for (const Folder& folder : state().folders) {
    if (folder.parentId == kRootId) {
      knownFolders.insert(folder.name.toCaseFolded());
    }
  }

  QList<StandardRule> rules;
  QStringList newFolders;
  for (const StandardRule& rule : standardRules()) {
    const RuleKey key{static_cast<int>(rule.field), static_cast<int>(rule.op),
                      rule.pattern.toCaseFolded()};
    if (present.contains(key)) {
      continue;
    }
    present.insert(key);
    rules.append(rule);
    // The first spelling of a missing top-level folder names it.
    const QString folderKey = rule.folder.toCaseFolded();
    if (!knownFolders.contains(folderKey)) {
      knownFolders.insert(folderKey);
      newFolders.append(rule.folder);
    }
  }
  if (rules.isEmpty()) {
    co_return 0;
  }
  if (auto appended =
          co_await m_db.appendNamedRules(kRootId, newFolders, rules);
      !appended) {
    co_return std::unexpected(appended.error());
  }
  if (auto committed = co_await commit(); !committed) {
    co_return std::unexpected(committed.error());
  }
  co_return static_cast<int>(rules.size());
}

// ── Read helpers ─────────────────────────────────────────────────────────────

FolderNode OrganizerService::tree(const std::optional<TreeQuery>& query) const {
  return buildTree(state(), allMods(), activeSet(), query);
}

Result<QStringList> OrganizerService::folderModUuids(qint64 folderId,
                                                     bool recursive) const {
  if (auto folder = findFolder(folderId); !folder) {
    return std::unexpected(folder.error());
  }
  const FolderNode root = tree();
  const FolderNode* node = findNode(root, folderId);
  return uuidsOf(recursive ? node->allMods() : node->mods);
}

Result<FolderToggle> OrganizerService::folderToggle(qint64 folderId,
                                                    bool recursive) const {
  if (auto folder = findFolder(folderId); !folder) {
    return std::unexpected(folder.error());
  }
  const FolderNode root = tree();
  const FolderNode* node = findNode(root, folderId);
  const QStringList uuids = uuidsOf(recursive ? node->allMods() : node->mods);
  const CheckState check = recursive ? node->check : node->ownCheck();
  if (check == CheckState::kOn) {
    return FolderToggle{QStringList{}, uuids};
  }
  return FolderToggle{uuids, QStringList{}};
}

Result<QSet<qint64>> OrganizerService::folderMoveTargets(
    qint64 folderId) const {
  auto folder = findNonRoot(folderId);
  if (!folder) {
    return std::unexpected(folder.error());
  }
  const QSet<qint64> subtree = subtreeIds(folderId);
  const int treeHeight = height(folderId);

  QSet<qint64> targets;
  for (const Folder& candidate : state().folders) {
    if (subtree.contains(candidate.id) || candidate.id == folder->parentId) {
      continue;
    }
    if (checkDepth(candidate.id, treeHeight) &&
        checkUniqueFolderName(candidate.id, folder->name)) {
      targets.insert(candidate.id);
    }
  }
  return targets;
}

QList<TreeFilter> OrganizerService::treeFilters() const {
  return organizer::treeFilters(allMods(), activeSet(), state());
}

}  // namespace px::modrim::organizer
