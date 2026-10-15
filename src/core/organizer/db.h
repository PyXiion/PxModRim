#pragma once

#include <QCoro/QCoroTask>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include "core/error.h"
#include "core/organizer/defaults.h"
#include "core/organizer/models.h"

namespace px::core::organizer {

// The organizer database file inside the config directory.
QString db_path(const QString& config_dir);

// SQLite store for the organizer. Every operation runs on one worker thread that owns the
// connection, so operations are serialized in submission order and each is atomic.
// The connection opens on first use and closes on close() or destruction.
class OrganizerDb {
public:
    explicit OrganizerDb(QString path);
    ~OrganizerDb();

    OrganizerDb(const OrganizerDb&) = delete;
    OrganizerDb& operator=(const OrganizerDb&) = delete;

    QCoro::Task<Result<OrganizerState>> load();

    QCoro::Task<Result<Folder>> create_folder(qint64 parent_id, QString name);
    QCoro::Task<Result<Folder>> create_folder_with(qint64 parent_id, QString name,
                                                   QStringList package_ids);
    QCoro::Task<Result<void>> rename_folder(qint64 folder_id, QString name);
    QCoro::Task<Result<void>> set_parent(qint64 folder_id, qint64 parent_id);
    QCoro::Task<Result<void>> set_collapsed(qint64 folder_id, bool collapsed);
    QCoro::Task<Result<void>> set_all_collapsed(bool collapsed);
    QCoro::Task<Result<void>> delete_folder(qint64 folder_id);

    QCoro::Task<Result<void>> place(QStringList package_ids, qint64 folder_id);
    QCoro::Task<Result<void>> unplace(QStringList package_ids);

    QCoro::Task<Result<Tag>> create_tag(QString name, QString color);
    QCoro::Task<Result<void>> update_tag(qint64 tag_id, QString name, QString color);
    QCoro::Task<Result<void>> delete_tag(qint64 tag_id);
    QCoro::Task<Result<void>> set_mod_tags(QStringList package_ids, QSet<qint64> add,
                                           QSet<qint64> remove);

    QCoro::Task<Result<QList<Rule>>> replace_rules(QList<RuleSpec> specs);
    // Creates `new_folders` under `parent_id`, then appends `rules` after the existing ones,
    // resolving each rule's folder by name among the children of `parent_id`.
    QCoro::Task<Result<void>> append_named_rules(qint64 parent_id, QStringList new_folders,
                                                 QList<StandardRule> rules);

    QCoro::Task<void> close();

private:
    // Runs `fn(QSqlDatabase&)` on the worker, opening the connection first if needed.
    template <class F>
    auto submit(F fn);

    QString path_;
    QString connection_name_;
    // One thread, never expiring: the connection stays on the thread that created it.
    QThreadPool pool_;
};

}  // namespace px::core::organizer
