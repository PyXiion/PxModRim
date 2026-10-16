#include "ui/panels/mod_info_panel.hpp"

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QVBoxLayout>
#include <algorithm>

#include <QCoro/QCoroFuture>
#include <QtConcurrent/QtConcurrentRun>

#include "core/downloads/manager.hpp"
#include "core/mod_service.hpp"
#include "core/services/startup_impact_service/labels.hpp"
#include "ui/components/procedural_preview.hpp"
#include "ui/config.hpp"
#include "ui/models/impact.hpp"
#include "ui/panels/mod_info_data.hpp"
#include "ui/panels/time_analytics_panel.hpp"
#include "ui/theme/constants.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {
class ModInfoBanner final : public QWidget {
 public:
  explicit ModInfoBanner(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override {
    return std::min(width * kBannerAspectH / kBannerAspectW, kBannerMaxHeight);
  }
  QSize sizeHint() const override {
    const int w = width() ? width() : 300;
    return {w, heightForWidth(w)};
  }
  void setTitle(QString title, QString subtitle) {
    m_title = std::move(title);
    m_subtitle = std::move(subtitle);
    update();
  }
  void setImage(QImage image, bool overlay) {
    m_image = std::move(image);
    m_overlay = overlay;
    rescale();
    update();
  }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    rescale();
    QWidget::resizeEvent(event);
  }
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setClipRect(rect());
    if (m_scaled.isNull())
      painter.fillRect(rect(), panelBgQ());
    else
      painter.drawImage((width() - m_scaled.width()) / 2,
                        (height() - m_scaled.height()) / 2, m_scaled);
    if (m_overlay && !m_title.isEmpty()) {
      QLinearGradient gradient(0, height() - kBannerOverlayHeight, 0, height());
      gradient.setColorAt(0, QColor(0, 0, 0, 0));
      gradient.setColorAt(1, QColor(0, 0, 0, 180));
      painter.fillRect(0, height() - kBannerOverlayHeight, width(),
                       kBannerOverlayHeight, gradient);
      const int top = height() - kBannerOverlayHeight + 12;
      int titleHeight = kBannerOverlayHeight - 24;
      if (!m_subtitle.isEmpty()) titleHeight = titleHeight * 3 / 5;
      auto font = painter.font();
      font.setPointSize(18);
      font.setBold(true);
      painter.setFont(font);
      painter.setPen(textMainQ());
      painter.drawText(QRect(16, top, width() - 32, titleHeight),
                       Qt::AlignLeft | Qt::AlignBottom, m_title);
      if (!m_subtitle.isEmpty()) {
        font.setPointSize(12);
        font.setBold(false);
        painter.setFont(font);
        painter.setPen(textMutedQ());
        painter.drawText(QRect(16, top + titleHeight + 2, width() - 32,
                               kBannerOverlayHeight - titleHeight - 26),
                         Qt::AlignLeft | Qt::AlignTop, m_subtitle);
      }
    }
  }

 private:
  void rescale() {
    m_scaled = m_image.isNull() || width() <= 0
                   ? QImage{}
                   : m_image.scaled(width(), heightForWidth(width()),
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
  }
  QImage m_image, m_scaled;
  QString m_title, m_subtitle;
  bool m_overlay = true;
};
namespace {
QString firstSentence(QString text) {
  static const QRegularExpression markup(
      R"(\[img\].*?\[/img\]|<[^>]+>|\[/?\w+(?:=[^\]]*)?\]|\[\*\])",
      QRegularExpression::CaseInsensitiveOption |
          QRegularExpression::DotMatchesEverythingOption);
  text.replace(markup, " ");
  text = QTextDocumentFragment::fromHtml(text).toPlainText().simplified();
  const auto end = text.indexOf('.');
  if (end >= 0) text.truncate(end + 1);
  if (text.size() > 80) {
    text.truncate(80);
    const auto space = text.lastIndexOf(' ');
    if (space >= 0) text.truncate(space);
    text += QChar(0x2026);
  }
  return text;
}
}  // namespace
ModInfoPanel::ModInfoPanel(CoreContext& ctx, QQmlEngine* engine, UiPrefs& prefs,
                           QWidget* parent)
    : QWidget(parent),
      m_ctx(ctx),
      m_engine(engine),
      m_prefs(prefs),
      m_banner(new ModInfoBanner(this)),
      m_placeholder(new QLabel("Select a mod to view details", this)),
      m_qml(engine ? new QQuickWidget(engine, this) : new QQuickWidget(this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  m_banner->hide();
  layout->addWidget(m_banner, 0, Qt::AlignTop);
  m_placeholder->setAlignment(Qt::AlignCenter);
  m_placeholder->setObjectName("placeholder");
  layout->addWidget(m_placeholder, 0, Qt::AlignTop);
  m_qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
  m_qml->setClearColor(paletteColor(u"ELEVATE_2"));
  m_qml->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/ModInfo.qml")));
  m_qml->hide();
  layout->addWidget(m_qml, 1);
  if (auto* root = m_qml->rootObject()) {
    root->setProperty("descExpanded", m_prefs.desc_expanded);
    connect(root, SIGNAL(openFolder()), this, SLOT(openFolder()));
    connect(root, SIGNAL(openUrl()), this, SLOT(openUrl()));
    connect(root, SIGNAL(selectMod(QString)), this,
            SIGNAL(modRequested(QString)));
    connect(root, SIGNAL(copyText(QString)), this, SLOT(copyText(QString)));
    connect(root, SIGNAL(openStartupDetails()), this,
            SLOT(openStartupDetails()));
    connect(root, SIGNAL(descToggled(bool)), this, SLOT(descToggled(bool)));
    connect(root, SIGNAL(updateMods()), this, SLOT(updateMod()));
  }
  setQml("downloadsBusy", m_ctx.downloads().isDownloading());
  m_busyConnection = m_ctx.downloads().busyChanged.connect([this](bool busy) {
    setQml("downloadsBusy", busy);
    syncUpdating();
  });
}
ModInfoPanel::~ModInfoPanel() {
  m_ctx.downloads().busyChanged.disconnect(m_busyConnection);
}
void ModInfoPanel::setQml(const char* name, const QVariant& value) {
  if (auto* root = m_qml->rootObject()) root->setProperty(name, value);
}
void ModInfoPanel::showMod(const ListedMod& mod,
                           const QList<view::ModIssueView>& issues) {
  const auto token = ++m_previewToken;
  m_mod = m_ctx.allMods().value(mod.uuid());
  if (!m_mod) {
    if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod))
      m_mod = std::make_shared<AboutXmlMod>(*about);
    else
      m_mod = std::make_shared<ListedMod>(mod);
  }
  m_placeholder->hide();
  m_banner->show();
  m_qml->show();
  const auto* about = dynamic_cast<const AboutXmlMod*>(&mod);
  m_banner->setTitle(mod.name, !mod.description.isEmpty()
                                   ? firstSentence(mod.description)
                               : about ? about->packageId.str()
                                       : QString{});
  ++m_startupToken;
  setQml("startup", QVariant{});
  setIssues(issues);
  syncUpdating();
  if (auto* root = m_qml->rootObject())
    QMetaObject::invokeMethod(root, "resetScroll");
  loadPreview(mod.modPath(), mod.name, token);
}
void ModInfoPanel::setIssues(const QList<view::ModIssueView>& issues) {
  if (!m_mod) return;
  auto data = buildModInfo(m_ctx, *m_mod, issues);
  const QString description = data.take("description").toString();
  setQml("info", data);
  if (description != m_description) {
    m_description = description;
    setQml("description", description);
  }
}
void ModInfoPanel::clear() {
  m_mod.reset();
  ++m_previewToken;
  ++m_startupToken;
  m_placeholder->show();
  m_banner->hide();
  m_qml->hide();
  m_description.clear();
  m_startupPid.reset();
  m_startupActivePids.clear();
  setQml("info", QVariant{});
  setQml("description", QString{});
  setQml("startup", QVariant{});
}
void ModInfoPanel::syncUpdating() {
  const auto id = m_mod ? m_ctx.downloads().updatableId(*m_mod) : std::nullopt;
  setQml("updatingThis", id && m_ctx.downloads().activeIds().contains(*id));
}
void ModInfoPanel::openFolder() {
  if (m_mod && m_mod->modPath())
    QDesktopServices::openUrl(QUrl::fromLocalFile(*m_mod->modPath()));
}
void ModInfoPanel::openUrl() {
  const auto* about = dynamic_cast<const AboutXmlMod*>(m_mod.get());
  if (!about) return;
  const QUrl url(about->url);
  const auto scheme = url.scheme().toLower();
  if (scheme == "http" || scheme == "https") QDesktopServices::openUrl(url);
}
void ModInfoPanel::copyText(const QString& text) {
  QGuiApplication::clipboard()->setText(text);
}
void ModInfoPanel::descToggled(bool expanded) {
  m_prefs.desc_expanded = expanded;
  if (m_ctx.hasConfigService()) saveUiPrefs(m_prefs, m_ctx.configService());
}
void ModInfoPanel::updateMod() {
  if (!m_mod) return;
  if (auto id = m_ctx.downloads().updatableId(*m_mod)) performUpdate(*id);
}
QCoro::Task<void> ModInfoPanel::performUpdate(QString id) {
  const auto result = co_await m_ctx.downloads().downloadMods({id});
  if (!result)
    qWarning().noquote() << "Mod update not started:" << result.error().message;
}
QCoro::Task<void> ModInfoPanel::loadPreview(std::optional<QString> path,
                                            QString title, quint64 token) {
  QPointer<ModInfoPanel> guard(this);
  const int width = m_banner->width() ? m_banner->width() : 300;
  const int height = m_banner->heightForWidth(width);
  auto future = QtConcurrent::run(
      [path = std::move(path), title = std::move(title), width, height] {
        QImage image;
        if (path) image.load(*path + "/About/Preview.png");
        const bool overlay = image.isNull();
        if (overlay) image = generatePreview(title, width, height);
        return std::pair{image, overlay};
      });
  auto [image, overlay] = co_await future;
  if (guard && token == guard->m_previewToken)
    guard->m_banner->setImage(std::move(image), overlay);
}
QCoro::Task<void> ModInfoPanel::setTimeAnalytics(
    const std::optional<QString>& pid, const QStringList& activePids) {
  const auto token = ++m_startupToken;
  m_startupPid = pid;
  m_startupActivePids = activePids;
  if (!pid) {
    setQml("startup",
           QVariantMap{{"available", false},
                       {"message", "Startup impact is unavailable."}});
    co_return;
  }
  QPointer<ModInfoPanel> guard(this);
  const auto selectedPid = *pid;
  const auto active = activePids;
  auto result =
      co_await m_ctx.modService().startupImpact().snapshot(active, selectedPid);
  if (!guard || token != guard->m_startupToken) co_return;
  if (!result || !result->report) {
    guard->setQml(
        "startup",
        QVariantMap{{"available", false},
                    {"message",
                     result ? QStringLiteral("No startup data recorded yet. "
                                             "Launch the game to generate it.")
                            : result.error().message}});
    co_return;
  }
  const auto& report = *result->report;
  const auto* mod = report.find(selectedPid, std::nullopt);
  const double own = result->selectedAvg;
  if (!mod || own < .001) {
    guard->setQml(
        "startup",
        QVariantMap{{"available", false},
                    {"message",
                     "This mod wasn't measured in the last recorded launch."}});
    co_return;
  }
  int rank = 1;
  for (const auto& entry : report.mods) rank += entry.totalImpactS > own;
  QList<std::pair<QString, double>> metrics;
  for (auto it = mod->metrics.cbegin(); it != mod->metrics.cend(); ++it)
    metrics.append({it.key(), it.value()});
  std::stable_sort(
      metrics.begin(), metrics.end(),
      [](const auto& a, const auto& b) { return a.second > b.second; });
  QVariantList rows;
  for (qsizetype i = 0; i < std::min<qsizetype>(3, metrics.size()); ++i)
    if (metrics[i].second >= .001)
      rows.append(
          QVariantMap{{"label", startup_impact::metricLabel(metrics[i].first)},
                      {"value", formatDuration(metrics[i].second)},
                      {"fraction", std::min(1.0, metrics[i].second / own)}});
  guard->setQml(
      "startup",
      QVariantMap{
          {"available", true},
          {"own", formatDuration(own)},
          {"rank",
           QString("#%1 slowest of %2 mods").arg(rank).arg(report.mods.size())},
          {"estimated", !active.contains(selectedPid, Qt::CaseInsensitive)},
          {"color", impactColor(own)},
          {"rows", rows}});
}
void ModInfoPanel::openStartupDetails() { showStartupDetails(); }
QCoro::Task<void> ModInfoPanel::showStartupDetails() {
  if (!m_startupDialog)
    m_startupDialog = new StartupImpactDialog(
        &m_ctx.modService().startupImpact(), m_engine, this);
  m_startupDialog->show();
  m_startupDialog->raise();
  m_startupDialog->activateWindow();
  co_await m_startupDialog->panel->setData(m_startupPid, m_startupActivePids);
}
}  // namespace px::modrim
