#include "ui/panels/update_dialog.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace px::ui {
UpdateDialog::UpdateDialog(const core::services::ReleaseInfo& release,
                           const QString& currentVersion, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Update Available"));
    resize(560, 460);
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(tr("PxModRim %1 is available").arg(release.tag), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
    layout->addWidget(title);
    layout->addWidget(new QLabel(tr("You are running %1.").arg(currentVersion), this));
    auto* notes = new QTextBrowser(this);
    notes->setOpenLinks(false);
    notes->setOpenExternalLinks(false);
    connect(notes, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        const QString scheme = url.scheme().toLower();
        if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
            QDesktopServices::openUrl(url);
    });
    notes->setMarkdown(release.notes.isEmpty() ? tr("No release notes.") : release.notes);
    layout->addWidget(notes, 1);
    auto* note = new QLabel(tr("Automatic updating may be added later. For now, downloading the release "
                              "yourself is the safest way to avoid update bugs."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QHBoxLayout;
    auto* skip = new QPushButton(tr("Skip This Version"), this);
    skip->setObjectName(QStringLiteral("appBtn"));
    connect(skip, &QPushButton::clicked, this, [this] { done(SKIP_RESULT); });
    auto* later = new QPushButton(tr("Later"), this);
    later->setObjectName(QStringLiteral("appBtn"));
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    auto* download = new QPushButton(tr("Download"), this);
    download->setObjectName(QStringLiteral("primaryAction"));
    download->setDefault(true);
    connect(download, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(skip);
    buttons->addStretch();
    buttons->addWidget(later);
    buttons->addWidget(download);
    layout->addLayout(buttons);
}
}  // namespace px::ui
