#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <QCoro/QCoroTask>

#include "core/error.hpp"
#include "core/organizer/defaults.hpp"
#include "core/organizer/models.hpp"

namespace px::modrim::organizer {

// The organizer database file inside the config directory.
QString dbPath(const QString& configDir);

// SQLite store for the organizer. Every operation runs on one worker thread
// that owns the connection, so operations are serialized in submission order
// and each is atomic. The connection opens on first use and closes on close()
// or destruction.
// Folder creation and reparenting enforce the depth limit and reject cycles
// against committed rows inside the mutation transaction.
class OrganizerDb {
 public:
  explicit OrganizerDb(QString path);
  ~OrganizerDb();

  OrganizerDb(const OrganizerDb&) = delete;
  OrganizerDb& operator=(const OrganizerDb&) = delete;

  QCoro::Task<Result<OrganizerState>> load();

  QCoro::Task<Result<Folder>> createFolder(qint64 parentId, QString name);
  QCoro::Task<Result<Folder>> createFolderWith(qint64 parentId, QString name,
                                               QStringList packageIds);
  QCoro::Task<Result<void>> renameFolder(qint64 folderId, QString name);
  QCoro::Task<Result<void>> setParent(qint64 folderId, qint64 parentId);
  QCoro::Task<Result<void>> setCollapsed(qint64 folderId, bool collapsed);
  QCoro::Task<Result<void>> setAllCollapsed(bool collapsed);
  QCoro::Task<Result<void>> deleteFolder(qint64 folderId);

  QCoro::Task<Result<void>> place(QStringList packageIds, qint64 folderId);
  QCoro::Task<Result<void>> unplace(QStringList packageIds);

  QCoro::Task<Result<Tag>> createTag(QString name, QString color);
  QCoro::Task<Result<void>> updateTag(qint64 tagId, QString name,
                                      QString color);
  QCoro::Task<Result<void>> deleteTag(qint64 tagId);
  QCoro::Task<Result<void>> setModTags(QStringList packageIds, QSet<qint64> add,
                                       QSet<qint64> remove);

  QCoro::Task<Result<QList<Rule>>> replaceRules(QList<RuleSpec> specs);
  // Creates `new_folders` under `parent_id`, then appends `rules` after the
  // existing ones, resolving each rule's folder by name among the children of
  // `parent_id`.
  QCoro::Task<Result<void>> appendNamedRules(qint64 parentId,
                                             QStringList newFolders,
                                             QList<StandardRule> rules);

  QCoro::Task<void> close();

 private:
  // Runs `fn(QSqlDatabase&)` on the worker, opening the connection first if
  // needed.
  template <class F>
  auto submit(F fn);

  QString m_path;
  QString m_connectionName;
  // One thread, never expiring: the connection stays on the thread that created
  // it.
  QThreadPool m_pool;
};

}  // namespace px::modrim::organizer
