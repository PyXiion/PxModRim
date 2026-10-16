#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <variant>

#include <QCoro/QCoroTask>

#include "core/context_fwd.hpp"
#include "core/error.hpp"
#include "core/events.hpp"
#include "core/organizer/db.hpp"
#include "core/organizer/models.hpp"
#include "core/organizer/resolve.hpp"
#include "core/plugin.hpp"

namespace px::modrim {
class CoreContext;
}

namespace px::modrim::organizer {

struct FolderToggle {
  QStringList enable;
  QStringList disable;
};

// Virtual folder tree, tags and auto-folder rules. Never touches load order.
// Reads mods and the active list from the context; each mutation persists,
// reloads the state from the database and then emits `changed`.
class OrganizerService : public Plugin {
 public:
  using AllModsFn = std::function<const QHash<QString, ListedModPtr>&()>;
  using ActiveUuidsFn = std::function<const QStringList&()>;

  OrganizerService(CoreContext& ctx, OrganizerDb& db);
  OrganizerService(AllModsFn allMods, ActiveUuidsFn activeUuids,
                   OrganizerDb& db);

  Event<std::monostate> changed;

  QCoro::Task<void> init();
  QCoro::Task<void> init(CoreContext&) override;
  QCoro::Task<void> shutdown() override;

  // Whether init() has loaded state; views built earlier must wait.
  bool ready() const noexcept;
  // Requires ready(): callers check before reading.
  const OrganizerState& state() const;

  // ── Folders ──────────────────────────────────────────────────────────────
  QCoro::Task<Result<Folder>> createFolder(QString name,
                                           qint64 parentId = kRootId);
  QCoro::Task<Result<Folder>> createFolderFrom(QString name,
                                               QStringList packageIds,
                                               qint64 parentId = kRootId);
  QCoro::Task<Result<void>> renameFolder(qint64 folderId, QString name);
  QCoro::Task<Result<void>> moveFolder(qint64 folderId, qint64 parentId);
  QCoro::Task<Result<void>> setCollapsed(qint64 folderId, bool collapsed);
  QCoro::Task<Result<void>> setAllCollapsed(bool collapsed);
  QCoro::Task<Result<void>> deleteFolder(qint64 folderId);

  // ── Placement ────────────────────────────────────────────────────────────
  QCoro::Task<Result<void>> place(QStringList packageIds, qint64 folderId);
  QCoro::Task<Result<void>> ungroup(QStringList packageIds);
  QCoro::Task<Result<void>> resetToRules(QStringList packageIds);

  // ── Tags ─────────────────────────────────────────────────────────────────
  QCoro::Task<Result<Tag>> createTag(QString name, QString color);
  QCoro::Task<Result<void>> updateTag(qint64 tagId, QString name,
                                      QString color);
  QCoro::Task<Result<void>> deleteTag(qint64 tagId);
  QCoro::Task<Result<void>> setModTags(QStringList packageIds, QSet<qint64> add,
                                       QSet<qint64> remove);

  // ── Rules ────────────────────────────────────────────────────────────────
  QCoro::Task<Result<void>> setRules(QList<RuleSpec> specs);
  // Appends the built-in rules not yet present. Returns how many were added.
  QCoro::Task<Result<int>> addStandardRules();

  // ── Read helpers ─────────────────────────────────────────────────────────
  FolderNode tree(const std::optional<TreeQuery>& query = std::nullopt) const;
  // Mods under `folder_id`; `recursive = false` keeps only its direct mods.
  Result<QStringList> folderModUuids(qint64 folderId,
                                     bool recursive = true) const;
  // (enable, disable) for a folder checkbox click: a fully enabled folder
  // disables everything; partial or off enables all.
  Result<FolderToggle> folderToggle(qint64 folderId,
                                    bool recursive = true) const;
  // Legal destinations, excluding the folder's current parent.
  Result<QSet<qint64>> folderMoveTargets(qint64 folderId) const;
  QList<TreeFilter> treeFilters() const;

 private:
  QCoro::Task<Result<void>> commit();

  Result<QString> validateNewFolder(const QString& name, qint64 parentId) const;
  Result<Folder> findFolder(qint64 folderId) const;
  Result<Folder> findNonRoot(qint64 folderId) const;
  Result<Tag> findTag(qint64 tagId) const;
  Result<QString> checkTag(const QString& name, const QString& color,
                           std::optional<qint64> exclude = std::nullopt) const;
  Result<void> checkUniqueFolderName(
      qint64 parentId, const QString& name,
      std::optional<qint64> exclude = std::nullopt) const;
  Result<void> checkDepth(qint64 parentId, int height) const;
  int depth(qint64 folderId) const;
  int height(qint64 folderId, int limit = kMaxDepth + 1) const;
  QSet<qint64> subtreeIds(qint64 folderId) const;

  const QHash<QString, ListedModPtr>& allMods() const;
  const QStringList& activeUuids() const;
  QSet<QString> activeSet() const;

  CoreContext* m_ctx = nullptr;
  AllModsFn m_allModsFn;
  ActiveUuidsFn m_activeUuidsFn;
  OrganizerDb& m_db;
  std::optional<OrganizerState> m_state;
};

}  // namespace px::modrim::organizer
