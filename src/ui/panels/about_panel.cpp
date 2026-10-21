#include "ui/panels/about_panel.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSysInfo>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <zip.h>

#include <pugixml.hpp>
#include <spdlog/version.h>

#include "core/version.hpp"
#include "ui/components/dialog_chrome.hpp"
#include "ui/context.hpp"
#include "ui/panels/upload_report_dialog.hpp"

namespace px::modrim {
QStringList AboutPanel::environment() {
#if defined(__clang__)
  const QString compiler = QStringLiteral("Clang " __clang_version__);
#elif defined(__GNUC__)
  const QString compiler = QStringLiteral("GCC " __VERSION__);
#elif defined(_MSC_VER)
  const QString compiler = QStringLiteral("MSVC %1").arg(_MSC_VER);
#else
  const QString compiler = QStringLiteral("C++23");
#endif
  return {QStringLiteral("C++23 (%1)").arg(compiler),
          QStringLiteral("Qt %1").arg(qVersion()),
          QStringLiteral("%1 %2 (%3)")
              .arg(QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                   QSysInfo::currentCpuArchitecture())};
}

QString AboutPanel::systemInformation() const {
  return (QStringList{QStringLiteral("PxModRim %1").arg(appVersion())} +
          environment())
      .join(u'\n');
}

QString AboutPanel::creditsHtml() {
  return QStringLiteral(
             "<h2>PxModRim</h2><p>Created and maintained by <b>PyXiion</b>. "
             "Licensed under GPL-3.0-or-later.</p>"
             "<h2>Inspiration</h2><p><a "
             "href='https://github.com/RimSort/RimSort'>RimSort</a></p>"
             "<h2>C++ dependencies</h2><table width='100%' cellpadding='5'>"
             "<tr><th align='left'>Library</th><th "
             "align='left'>Version</th><th align='left'>License</th></tr>"
             "<tr><td><a "
             "href='https://www.qt.io/'>Qt</a></td><td>%1</td><td>LGPL-3.0 / "
             "GPL-3.0 / commercial</td></tr>"
             "<tr><td><a "
             "href='https://qcoro.dvratil.cz/'>QCoro</a></td><td>Build-linked</"
             "td><td>MIT</td></tr>"
             "<tr><td><a "
             "href='https://pugixml.org/'>pugixml</a></td><td>%2.%3.%4</"
             "td><td>MIT</td></tr>"
             "<tr><td><a "
             "href='https://libzip.org/'>libzip</a></td><td>%5</"
             "td><td>BSD-3-Clause</td></tr>"
             "<tr><td><a "
             "href='https://github.com/gabime/spdlog'>spdlog</a></"
             "td><td>%6.%7.%8</td><td>MIT</td></tr>"
             "<tr><td><a "
             "href='https://github.com/stephenberry/glaze'>Glaze</a></"
             "td><td>5.5.4 (build dependency)</td><td>MIT</td></tr>"
             "</table>")
      .arg(QString::fromLatin1(qVersion()))
      .arg(PUGIXML_VERSION / 1000)
      .arg(PUGIXML_VERSION / 10 % 100)
      .arg(PUGIXML_VERSION % 10)
      .arg(QString::fromLatin1(zip_libzip_version()))
      .arg(SPDLOG_VER_MAJOR)
      .arg(SPDLOG_VER_MINOR)
      .arg(SPDLOG_VER_PATCH);
}

AboutPanel::AboutPanel(QWidget* parent, CoreContext& ctx, Notifier& toast)
    : QDialog(parent), m_ctx(ctx), m_toast(toast) {
  setObjectName(QStringLiteral("aboutPanel"));
  setWindowTitle(tr("About PxModRim"));
  setModal(true);
  resize(700, 740);
  setMinimumSize(640, 620);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 20);
  layout->setSpacing(18);
  auto* hero = new QFrame(this);
  hero->setObjectName(QStringLiteral("aboutHero"));
  auto* heroLayout = new QHBoxLayout(hero);
  heroLayout->setContentsMargins(20, 20, 20, 20);
  heroLayout->setSpacing(20);
  auto* logo = new QLabel(hero);
  logo->setPixmap(
      QPixmap(QStringLiteral(":/qt/qml/PxModRim/assets/logo.svg"))
          .scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  heroLayout->addWidget(logo, 0, Qt::AlignTop);
  auto* identity = new QVBoxLayout;
  identity->setSpacing(6);
  auto* titleRow = new QHBoxLayout;
  auto* title = new QLabel(QStringLiteral("PxModRim"), hero);
  title->setObjectName(QStringLiteral("aboutTitle"));
  titleRow->addWidget(title);
  titleRow->addStretch();
  auto* version = new QLabel(QStringLiteral("v%1").arg(appVersion()), hero);
  version->setObjectName(QStringLiteral("aboutVersionBadge"));
  titleRow->addWidget(version, 0, Qt::AlignVCenter);
  identity->addLayout(titleRow);
  auto* tagline =
      new QLabel(tr("A friendly, modern mod manager for RimWorld."), hero);
  tagline->setObjectName(QStringLiteral("aboutTagline"));
  identity->addWidget(tagline);
  auto* description = new QLabel(tr("Scan, sort, resolve dependencies, and "
                                    "manage large mod lists easily."),
                                 hero);
  description->setObjectName(QStringLiteral("aboutDescription"));
  description->setWordWrap(true);
  identity->addWidget(description);
  identity->addStretch();
  heroLayout->addLayout(identity, 1);
  layout->addWidget(hero);

  auto* section = new QWidget(this);
  auto* sectionLayout = new QVBoxLayout(section);
  sectionLayout->setContentsMargins(0, 0, 0, 0);
  sectionLayout->setSpacing(6);
  auto* heading = new QLabel(tr("THE PROJECT"), section);
  heading->setObjectName(QStringLiteral("aboutSectionTitle"));
  sectionLayout->addWidget(heading);
  auto* story = new QLabel(
      tr("I used RimPy and RimSort for a long time myself, but with recent "
         "updates it kept getting "
         "slower and worse — some features even broke my game 🙁<br><br>"
         "I initially started working on a PR to fix most of the parallelism "
         "issues in RimSort, "
         "but realized it didn't make much sense. So I decided to try creating "
         "my own alternative "
         "with the help of AI. The result is what you see on your "
         "screen.<br><br>"
         "This is all just my personal opinion based on my experience with "
         "RimSort at the time, "
         "and things may have changed for the better since then (I hope "
         "so!).<br><br><b>— PyXiion</b>"),
      section);
  story->setObjectName(QStringLiteral("aboutStory"));
  story->setWordWrap(true);
  story->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  auto* scroll = new QScrollArea(section);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(story);
  scroll->viewport()->setAutoFillBackground(false);
  scroll->viewport()->setBackgroundRole(QPalette::NoRole);
  story->setAutoFillBackground(false);
  story->setBackgroundRole(QPalette::NoRole);
  sectionLayout->addWidget(scroll, 1);
  layout->addWidget(section, 1);

  auto* details = new QFrame(this);
  details->setObjectName(QStringLiteral("aboutDetails"));
  auto* grid = new QGridLayout(details);
  grid->setContentsMargins(16, 14, 16, 14);
  grid->setHorizontalSpacing(14);
  grid->setVerticalSpacing(8);
  const QList<QPair<QString, QString>> rows{
      {tr("CREATED BY"), QStringLiteral("PyXiion")},
      {tr("LICENSE"), QStringLiteral("GPL-3.0-or-later")},
      {tr("INSPIRED BY"), QStringLiteral("RimSort")},
      {tr("ENVIRONMENT"), environment().join(QStringLiteral("  •  "))}};
  for (qsizetype row = 0; row < rows.size(); ++row) {
    auto* label = new QLabel(rows[row].first, details);
    label->setObjectName(QStringLiteral("aboutMetaLabel"));
    auto* value = new QLabel(rows[row].second, details);
    value->setObjectName(QStringLiteral("aboutMetaValue"));
    value->setWordWrap(true);
    grid->addWidget(label, static_cast<int>(row), 0);
    grid->addWidget(value, static_cast<int>(row), 1);
  }
  grid->setColumnStretch(1, 1);
  layout->addWidget(details);

  auto* credits = new QDialog(this);
  credits->setObjectName(QStringLiteral("creditsDialog"));
  credits->setWindowTitle(tr("Open-Source Credits"));
  credits->setModal(true);
  credits->resize(560, 500);
  auto* creditsLayout = new QVBoxLayout(credits);
  creditsLayout->setContentsMargins(20, 20, 20, 16);
  creditsLayout->setSpacing(14);
  auto* creditsTitle = new QLabel(tr("Open-source credits"), credits);
  creditsTitle->setObjectName(QStringLiteral("creditsTitle"));
  creditsLayout->addWidget(creditsTitle);
  auto* browser = new QTextBrowser(credits);
  browser->setObjectName(QStringLiteral("creditsBrowser"));
  browser->setOpenExternalLinks(true);
  browser->setHtml(creditsHtml());
  creditsLayout->addWidget(browser);
  auto* creditsFooter = new QHBoxLayout;
  creditsFooter->addStretch();
  auto* creditsClose = new QPushButton(tr("Close"), credits);
  creditsClose->setObjectName(QStringLiteral("appBtn"));
  connect(creditsClose, &QPushButton::clicked, credits, &QDialog::reject);
  creditsFooter->addWidget(creditsClose);
  creditsLayout->addLayout(creditsFooter);
  installDialogChrome(credits);

  auto* actions = new QHBoxLayout;
  actions->setSpacing(8);
  auto* github = new QPushButton(tr("GitHub"), this);
  github->setObjectName(QStringLiteral("appBtn"));
  connect(github, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://github.com/PyXiion/PxModRim")));
  });
  actions->addWidget(github);
  auto* issues = new QPushButton(tr("Report issue"), this);
  issues->setObjectName(QStringLiteral("appBtn"));
  connect(issues, &QPushButton::clicked, this,
          [] { QDesktopServices::openUrl(QUrl(QString(kIssuesUrl))); });
  actions->addWidget(issues);
  auto* copy = new QPushButton(tr("Copy system info"), this);
  copy->setObjectName(QStringLiteral("appBtn"));
  connect(copy, &QPushButton::clicked, this, [this] {
    QApplication::clipboard()->setText(systemInformation());
    m_toast.success(tr("System info copied"));
  });
  actions->addWidget(copy);
  m_upload = new QPushButton(tr("Upload log && system info…"), this);
  m_upload->setObjectName(QStringLiteral("appBtn"));
  connect(m_upload, &QPushButton::clicked, this, [this] { uploadReport(); });
  actions->addWidget(m_upload);
  auto* showCredits = new QPushButton(tr("Open-source credits"), this);
  showCredits->setObjectName(QStringLiteral("appBtn"));
  connect(showCredits, &QPushButton::clicked, credits, &QDialog::open);
  actions->addWidget(showCredits);
  actions->addStretch();
  layout->addLayout(actions);
  auto* disclaimer =
      new QLabel(tr("PxModRim is an unofficial fan-made tool. RimWorld is a "
                    "trademark of Ludeon Studios."),
                 this);
  disclaimer->setObjectName(QStringLiteral("aboutDisclaimer"));
  disclaimer->setWordWrap(true);
  layout->addWidget(disclaimer);
  auto* footer = new QHBoxLayout;
  footer->addStretch();
  auto* close = new QPushButton(tr("Close"), this);
  close->setObjectName(QStringLiteral("primaryAction"));
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  footer->addWidget(close);
  layout->addLayout(footer);
}

QCoro::Task<void> AboutPanel::uploadReport() {
  QPointer<QPushButton> button(m_upload);
  m_upload->setEnabled(false);
  co_await handleUploadReport(this, m_ctx, m_toast);
  if (button) button->setEnabled(true);
}
}  // namespace px::modrim
