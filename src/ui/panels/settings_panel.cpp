#include "ui/panels/settings_panel.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QVBoxLayout>

#include "core/mod_service.hpp"
#include "core/sort/community_service.hpp"
#include "ui/components/dialogs.hpp"
#include "ui/context.hpp"
#include "ui/theme/palette.hpp"
namespace px::modrim {
namespace {
class FolderDialog : public QFileDialog {
 public:
  FolderDialog(QWidget* parent, const QString& title)
      : QFileDialog(parent, title) {
    setFileMode(QFileDialog::Directory);
    setOption(QFileDialog::ShowDirsOnly);
  }
};
QString describeScaleOverride(const ResolvedScale& scale) {
  if (scale.source == ScaleSource::kSaved) return {};
  if (scale.source == ScaleSource::kQtEnv) {
    return QStringLiteral(
               "Overridden by the QT_SCALE_FACTOR environment variable "
               "(%1); this setting has no effect while it is set.")
        .arg(scale.qtScaleFactor);
  }
  const QString origin = scale.source == ScaleSource::kCli
                             ? QStringLiteral("--ui-scale")
                             : QStringLiteral("PX_UI_SCALE");
  return QStringLiteral(
             "This session uses %1% from %2; the saved value applies "
             "when that is removed.")
      .arg(scale.scale)
      .arg(origin);
}
}  // namespace
SettingsBackend::SettingsBackend(CoreContext& ctx, QDialog* dialog,
                                 const std::vector<SettingsSection*>& sections,
                                 int uiScale, const ResolvedScale& initialScale)
    : QObject(dialog),
      m_ctx(ctx),
      m_dialog(dialog),
      m_scaleOverrideNote(describeScaleOverride(initialScale)) {
  for (auto* section : sections)
    m_sections.append(QVariantMap{
        {"title", section->title()},
        {"source", section->source().toString()},
        {"section", QVariant::fromValue(static_cast<QObject*>(section))}});
  const auto& cfg = ctx.config();
  const auto text = [](const std::string& value) {
    return QString::fromStdString(value);
  };
  m_initial = {{"game", text(cfg.paths.game)},
               {"local", text(cfg.paths.local)},
               {"workshop", text(cfg.paths.workshop)},
               {"config", text(cfg.paths.config_folder)},
               {"compact", cfg.compact_mod_list},
               {"uiScale", uiScale},
               {"launchArgs", text(cfg.launch_args)},
               {"launchWrapper", text(cfg.launch_wrapper)},
               {"afterLaunch", static_cast<int>(cfg.after_launch)},
               {"confirmErrors", cfg.launch_confirm_errors},
               {"confirmUnsaved", cfg.launch_confirm_unsaved},
               {"confirmRunning", cfg.launch_confirm_running},
               {"useAltIds", cfg.sort.use_alternative_package_ids},
               {"checkMissing", cfg.sort.check_missing_dependencies},
               {"useCommunity", cfg.sort.use_community_rules}};
  const auto path = communityRulesFile();
  m_communityStatus = QFileInfo::exists(path)
                          ? "Found: " + path
                          : QStringLiteral("Not downloaded");
  m_cacheAvailable = !cfg.paths.config_folder.empty();
  m_cacheStatus = m_cacheAvailable
                      ? QStringLiteral("Cache ready")
                      : QStringLiteral("No config folder — not available");
}
QCoro::Task<void> SettingsBackend::browse(QString key) {
  static const QMap<QString, QString> titles{
      {"game", "Select RimWorld Game Folder"},
      {"local", "Select Local Mods Folder"},
      {"workshop", "Select Workshop Mods Folder"},
      {"config", "Select RimWorld Config Folder"}};
  if (!titles.contains(key)) co_return;
  QPointer<SettingsBackend> guard(this);
  const auto [result, picker] =
      co_await awaitDialog<FolderDialog>(m_dialog, titles.value(key));
  const auto files = picker->selectedFiles();
  if (guard && result == QDialog::Accepted && !files.isEmpty())
    emit guard->pathPicked(key, files.front());
}
QString SettingsBackend::localCandidate(const QString& gamePath) const {
  const auto path = QDir(gamePath).filePath(QStringLiteral("Mods"));
  return QFileInfo(path).isDir() ? path : QString{};
}
void SettingsBackend::autoDetect() {
  const auto paths = detectGamePaths();
  const std::pair<const char*, const std::string*> values[]{
      {"game", &paths.game},
      {"local", &paths.local},
      {"workshop", &paths.workshop},
      {"config", &paths.config_folder}};
  for (const auto& [key, value] : values)
    if (!value->empty())
      emit pathPicked(QString::fromLatin1(key), QString::fromStdString(*value));
}
QCoro::Task<void> SettingsBackend::downloadCommunityRules() {
  m_communityStatus = QStringLiteral("Downloading…");
  m_communityBusy = true;
  emit communityChanged();
  if (!m_ctx.hasConfigService()) {
    m_communityStatus = QStringLiteral("Download failed");
    m_communityBusy = false;
    emit communityChanged();
    co_return;
  }
  LoadingState loading;
  auto* progress =
      new QProgressDialog(QStringLiteral("Downloading community rules…"),
                          QString{}, 0, 100, m_dialog);
  progress->setWindowModality(Qt::WindowModal);
  progress->setCancelButton(nullptr);
  progress->setMinimumDuration(0);
  connect(&loading, &LoadingState::changed, progress,
          [progress](const QString& status, int percent, int, int) {
            progress->setLabelText(status);
            progress->setValue(percent);
          });
  progress->show();
  sort::CommunityRulesService service(m_ctx.configService(), m_ctx.network());
  QPointer<SettingsBackend> guard(this);
  QPointer<QProgressDialog> progressGuard(progress);
  const auto path = co_await service.ensureRules(loading, true);
  if (progressGuard) {
    progressGuard->close();
    progressGuard->deleteLater();
  }
  if (!guard) co_return;
  guard->m_communityStatus =
      path ? "Downloaded: " + *path : QStringLiteral("Download failed");
  guard->m_communityBusy = false;
  emit guard->communityChanged();
}
QCoro::Task<void> SettingsBackend::clearCache() {
  m_cacheStatus = QStringLiteral("Clearing…");
  m_cacheBusy = true;
  emit cacheChanged();
  QPointer<SettingsBackend> guard(this);
  const auto result = co_await m_ctx.modService().startupImpact().clear();
  if (!guard) co_return;
  guard->m_cacheStatus = result ? QStringLiteral("Cache cleared")
                                : "Error: " + result.error().message;
  guard->m_cacheBusy = false;
  emit guard->cacheChanged();
}
void SettingsBackend::reportSaveError(const QString& message) {
  auto* box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Settings"),
                              message, QMessageBox::Ok, m_dialog);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->open();
}
SettingsPanel::SettingsPanel(
    CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
    const std::vector<SettingsSectionFactory>& factories, int uiScale,
    const ResolvedScale& initialScale)
    : QDialog(parent),
      m_config(ctx.config()),
      m_uiScale(uiScale),
      m_backend(nullptr),
      m_qml(new QQuickWidget(engine, this)) {
  setObjectName(QStringLiteral("settingsPanel"));
  setWindowTitle(QStringLiteral("Settings"));
  setModal(true);
  resize(700, 560);
  for (const auto& factory : factories) m_sections.push_back(factory(this));
  m_backend =
      new SettingsBackend(ctx, this, m_sections, m_uiScale, initialScale);
  connect(m_backend, &SettingsBackend::saveRequested, this,
          &SettingsPanel::save);
  connect(m_backend, &SettingsBackend::cancelRequested, this, &QDialog::reject);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  m_qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
  m_qml->setAttribute(Qt::WA_AlwaysStackOnTop, false);
  m_qml->setClearColor(paletteColor(u"ELEVATE_2"));
  m_qml->rootContext()->setContextProperty(QStringLiteral("settings"),
                                           m_backend);
  m_qml->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/Settings.qml")));
  layout->addWidget(m_qml);
  // A QML click handler must finish before its root object is destroyed.
  connect(
      this, &QDialog::finished, this, [this] { m_qml->setSource(QUrl{}); },
      Qt::QueuedConnection);
}
SettingsPanel::SettingsPanel(AppContext& app, QQmlEngine* engine,
                             QWidget* parent)
    : SettingsPanel(app.coreContext(), engine, parent, app.settingsSections(),
                    app.uiPrefs().ui_scale, app.initialScale()) {}
SettingsPanel::~SettingsPanel() { m_qml->setSource(QUrl{}); }
void SettingsPanel::save(const QVariantMap& values) {
  auto updated = m_config;
  const auto text = [&](const char* key) {
    return values.value(QLatin1String(key)).toString().trimmed().toStdString();
  };
  updated.paths.game = text("game");
  updated.paths.local = text("local");
  updated.paths.workshop = text("workshop");
  updated.paths.config_folder = text("config");
  updated.sort.use_alternative_package_ids = values.value("useAltIds").toBool();
  updated.sort.check_missing_dependencies =
      values.value("checkMissing").toBool();
  updated.sort.use_community_rules = values.value("useCommunity").toBool();
  updated.compact_mod_list = values.value("compact").toBool();
  updated.launch_args = text("launchArgs");
  updated.launch_wrapper = text("launchWrapper");
  updated.after_launch =
      static_cast<AfterLaunch>(values.value("afterLaunch").toInt());
  updated.launch_confirm_errors = values.value("confirmErrors").toBool();
  updated.launch_confirm_unsaved = values.value("confirmUnsaved").toBool();
  updated.launch_confirm_running = values.value("confirmRunning").toBool();
  for (auto* section : m_sections) {
    const auto result = section->apply();
    if (!result) {
      m_backend->reportSaveError(result.error().message);
      return;
    }
  }
  m_config = std::move(updated);
  m_uiScale = clampUiScale(values.value("uiScale", 100).toInt());
  accept();
}
}  // namespace px::modrim
