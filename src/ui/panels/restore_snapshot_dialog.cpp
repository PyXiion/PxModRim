#include "ui/panels/restore_snapshot_dialog.h"

#include <algorithm>

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStringView>
#include <QTime>
#include <QTimeZone>
#include <QVBoxLayout>

#include "core/mods_config.h"

namespace px::ui {

namespace {

constexpr QStringView kSnapshotPrefix = u"ModsConfig_";

QString snapshotStem(const QString& path) {
    const QString name = QFileInfo(path).fileName();
    const qsizetype dot = name.lastIndexOf(u'.');
    return dot > 0 ? name.left(dot) : name;
}

bool allAsciiDigits(QStringView text) {
    return std::all_of(text.begin(), text.end(), [](QChar c) { return c >= u'0' && c <= u'9'; });
}

// The UTC time encoded in a snapshot name such as ModsConfig_20260922_120000[_suffix].xml.
// Non-digit or out-of-range fields are rejected like Python's strptime rejected them.
std::optional<QDateTime> timestampFromStem(const QString& stem) {
    if (!stem.startsWith(kSnapshotPrefix)) {
        return std::nullopt;
    }
    const QStringList parts = stem.mid(kSnapshotPrefix.size()).split(u'_');
    if (parts.size() < 2 || parts[0].size() != 8 || parts[1].size() < 6) {
        return std::nullopt;
    }
    const QString date = parts[0];
    const QString time = parts[1].left(6);
    if (!allAsciiDigits(date) || !allAsciiDigits(time)) {
        return std::nullopt;
    }
    const QDate day(date.left(4).toInt(), date.mid(4, 2).toInt(), date.mid(6, 2).toInt());
    const QTime time_of_day(time.left(2).toInt(), time.mid(2, 2).toInt(), time.mid(4, 2).toInt());
    if (!day.isValid() || !time_of_day.isValid()) {
        return std::nullopt;
    }
    return QDateTime(day, time_of_day, QTimeZone::utc());
}

QString formatUtc(const QDateTime& when) {
    return when.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral(" UTC");
}

// The name's timestamp when it has one, otherwise the file's modification time, otherwise the stem.
QString snapshotTimestamp(const QString& path) {
    const QString stem = snapshotStem(path);
    if (const auto named = timestampFromStem(stem)) {
        return formatUtc(*named);
    }
    const QDateTime modified = QFileInfo(path).lastModified();
    return modified.isValid() ? formatUtc(modified) : stem;
}

}  // namespace

RestoreSnapshotDialog::RestoreSnapshotDialog(const QStringList& snapshots, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("restoreSnapshotDialog"));
    setWindowTitle(QStringLiteral("Restore Mod List"));
    resize(560, 420);

    snapshots_.reserve(static_cast<std::size_t>(snapshots.size()));
    for (const QString& path : snapshots) {
        snapshots_.push_back(Snapshot{path, core::parse_mods_config(path)});
    }

    auto* layout = new QVBoxLayout(this);

    auto* instruction = new QLabel(
        QStringLiteral("Choose a saved mod list. Your current list will be backed up before it is replaced."), this);
    instruction->setWordWrap(true);
    layout->addWidget(instruction);

    list_ = new QListWidget(this);
    populateList();
    layout->addWidget(list_);

    details_ = new QLabel(this);
    details_->setWordWrap(true);
    layout->addWidget(details_);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();

    restore_ = new QPushButton(QStringLiteral("Restore"), this);
    restore_->setObjectName(QStringLiteral("primaryAction"));
    restore_->setEnabled(false);
    connect(restore_, &QPushButton::clicked, this, &RestoreSnapshotDialog::onRestoreClicked);

    auto* cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("appBtn"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    buttons->addWidget(cancel);
    buttons->addWidget(restore_);
    layout->addLayout(buttons);

    connect(list_, &QListWidget::currentItemChanged, this, &RestoreSnapshotDialog::onCurrentItemChanged);
    connect(list_, &QListWidget::itemDoubleClicked, this, &RestoreSnapshotDialog::onRestoreClicked);

    // Connected before selecting, so the initial selection updates the details and the button.
    if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }
}

std::optional<QString> RestoreSnapshotDialog::selectedSnapshot() const {
    return selected_;
}

void RestoreSnapshotDialog::populateList() {
    for (std::size_t i = 0; i < snapshots_.size(); ++i) {
        const Snapshot& snapshot = snapshots_[i];
        auto* item = new QListWidgetItem(list_);
        const QString timestamp = snapshotTimestamp(snapshot.path);

        if (snapshot.parsed) {
            const qsizetype active = snapshot.parsed->activeMods.size();
            const QString noun = active == 1 ? QStringLiteral("mod") : QStringLiteral("mods");
            item->setText(QStringLiteral(u"%1 \u2014 %2 active %3, RimWorld %4")
                              .arg(timestamp)
                              .arg(QString::number(active))
                              .arg(noun)
                              .arg(snapshot.parsed->version));
        } else {
            item->setText(QStringLiteral(u"%1 \u2014 Cannot be read").arg(timestamp));
        }
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setToolTip(QFileInfo(snapshot.path).fileName());
    }
}

void RestoreSnapshotDialog::onCurrentItemChanged(QListWidgetItem* current) {
    if (current == nullptr) {
        selected_.reset();
        restore_->setEnabled(false);
        details_->clear();
        return;
    }

    const Snapshot& snapshot = snapshots_[static_cast<std::size_t>(current->data(Qt::UserRole).toInt())];
    const QString name = QFileInfo(snapshot.path).fileName();
    if (snapshot.parsed) {
        selected_ = snapshot.path;
        restore_->setEnabled(true);
        details_->setText(QStringLiteral("<b>File:</b> %1<br><b>Active mods:</b> %2 | <b>Game version:</b> %3")
                              .arg(name)
                              .arg(QString::number(snapshot.parsed->activeMods.size()))
                              .arg(snapshot.parsed->version));
    } else {
        selected_.reset();
        restore_->setEnabled(false);
        details_->setText(QStringLiteral("<b>This saved mod list cannot be read.</b> %1 will not be restored.")
                              .arg(name));
    }
}

void RestoreSnapshotDialog::onRestoreClicked() {
    if (selected_) {
        accept();
    }
}

ConfirmRestoreDialog::ConfirmRestoreDialog(const QString& snapshotName, QWidget* parent) : QMessageBox(parent) {
    setIcon(QMessageBox::Question);
    // The QMessageBox setter is a no-op on macOS; the QWidget setter applies there too.
    QDialog::setWindowTitle(QStringLiteral("Restore Mod List"));
    setText(QStringLiteral("Restore the mod list saved in %1?").arg(snapshotName));
    setInformativeText(QStringLiteral(
        "This replaces your current active mod list. PxModRim will save a backup of the current list first."));
    setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    setDefaultButton(QMessageBox::Cancel);
    if (auto* yes = qobject_cast<QPushButton*>(button(QMessageBox::Yes))) {
        yes->setText(QStringLiteral("Restore"));
        yes->setObjectName(QStringLiteral("primaryAction"));
    }
}

}  // namespace px::ui
