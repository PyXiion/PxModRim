#include "ui/panels/settings_panel.h"
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
#include "core/mod_service.h"
#include "core/sort/community_service.h"
#include "ui/components/dialogs.h"
#include "ui/context.h"
#include "ui/theme/palette.h"
namespace px::ui {
namespace {
class FolderDialog : public QFileDialog {
public:
    FolderDialog(QWidget* parent, const QString& title) : QFileDialog(parent, title) {
        setFileMode(QFileDialog::Directory);
        setOption(QFileDialog::ShowDirsOnly);
    }
};
}
SettingsBackend::SettingsBackend(core::CoreContext& ctx, QDialog* dialog, const std::vector<SettingsSection*>& sections)
    : QObject(dialog), ctx_(ctx), dialog_(dialog) {
    for (auto* section : sections)
        sections_.append(QVariantMap{{"title", section->title()}, {"source", section->source().toString()},
                                     {"section", QVariant::fromValue(static_cast<QObject*>(section))}});
    const auto& cfg = ctx.config();
    const auto text = [](const std::string& value) { return QString::fromStdString(value); };
    initial_ = {{"game", text(cfg.paths.game)}, {"local", text(cfg.paths.local)},
        {"workshop", text(cfg.paths.workshop)}, {"config", text(cfg.paths.config_folder)},
        {"compact", cfg.compact_mod_list}, {"launchArgs", text(cfg.launch_args)}, {"launchWrapper", text(cfg.launch_wrapper)},
        {"afterLaunch", static_cast<int>(cfg.after_launch)}, {"confirmErrors", cfg.launch_confirm_errors},
        {"confirmUnsaved", cfg.launch_confirm_unsaved}, {"confirmRunning", cfg.launch_confirm_running},
        {"useAltIds", cfg.sort.use_alternative_package_ids}, {"checkMissing", cfg.sort.check_missing_dependencies},
        {"useCommunity", cfg.sort.use_community_rules}};
    const auto path = core::community_rules_file();
    communityStatus_ = QFileInfo::exists(path) ? "Found: " + path : QStringLiteral("Not downloaded");
    cacheAvailable_ = !cfg.paths.config_folder.empty();
    cacheStatus_ = cacheAvailable_ ? QStringLiteral("Cache ready") : QStringLiteral("No config folder — not available");
}
QCoro::Task<void> SettingsBackend::browse(QString key) {
    static const QMap<QString, QString> titles{{"game", "Select RimWorld Game Folder"}, {"local", "Select Local Mods Folder"},
        {"workshop", "Select Workshop Mods Folder"}, {"config", "Select RimWorld Config Folder"}};
    if (!titles.contains(key)) co_return;
    QPointer<SettingsBackend> guard(this);
    const auto [result, picker] = co_await awaitDialog<FolderDialog>(dialog_, titles.value(key));
    const auto files = picker->selectedFiles();
    if (guard && result == QDialog::Accepted && !files.isEmpty()) emit guard->pathPicked(key, files.front());
}
QString SettingsBackend::localCandidate(const QString& gamePath) const {
    const auto path = QDir(gamePath).filePath(QStringLiteral("Mods"));
    return QFileInfo(path).isDir() ? path : QString{};
}
void SettingsBackend::autoDetect() {
    const auto paths = core::detect_game_paths();
    const std::pair<const char*, const std::string*> values[]{{"game", &paths.game}, {"local", &paths.local},
        {"workshop", &paths.workshop}, {"config", &paths.config_folder}};
    for (const auto& [key, value] : values)
        if (!value->empty()) emit pathPicked(QString::fromLatin1(key), QString::fromStdString(*value));
}
QCoro::Task<void> SettingsBackend::downloadCommunityRules() {
    communityStatus_ = QStringLiteral("Downloading…");
    communityBusy_ = true;
    emit communityChanged();
    if (!ctx_.hasConfigService()) {
        communityStatus_ = QStringLiteral("Download failed");
        communityBusy_ = false;
        emit communityChanged();
        co_return;
    }
    core::LoadingState loading;
    auto* progress = new QProgressDialog(QStringLiteral("Downloading community rules…"), QString{}, 0, 100, dialog_);
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    connect(&loading, &core::LoadingState::changed, progress,
        [progress](const QString& status, int percent, int, int) { progress->setLabelText(status); progress->setValue(percent); });
    progress->show();
    core::sort::CommunityRulesService service(ctx_.configService(), ctx_.network());
    QPointer<SettingsBackend> guard(this);
    QPointer<QProgressDialog> progressGuard(progress);
    const auto path = co_await service.ensure_rules(loading, true);
    if (progressGuard) { progressGuard->close(); progressGuard->deleteLater(); }
    if (!guard) co_return;
    guard->communityStatus_ = path ? "Downloaded: " + *path : QStringLiteral("Download failed");
    guard->communityBusy_ = false;
    emit guard->communityChanged();
}
QCoro::Task<void> SettingsBackend::clearCache() {
    cacheStatus_ = QStringLiteral("Clearing…");
    cacheBusy_ = true;
    emit cacheChanged();
    QPointer<SettingsBackend> guard(this);
    const auto result = co_await ctx_.modService().startup_impact().clear();
    if (!guard) co_return;
    guard->cacheStatus_ = result ? QStringLiteral("Cache cleared") : "Error: " + result.error().message;
    guard->cacheBusy_ = false;
    emit guard->cacheChanged();
}
void SettingsBackend::reportSaveError(const QString& message) {
    auto* box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Settings"), message, QMessageBox::Ok, dialog_);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}
SettingsPanel::SettingsPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent,
                             const std::vector<SettingsSectionFactory>& factories)
    : QDialog(parent), config_(ctx.config()), backend_(nullptr), qml_(new QQuickWidget(engine, this)) {
    setObjectName(QStringLiteral("settingsPanel"));
    setWindowTitle(QStringLiteral("Settings"));
    setModal(true);
    resize(700, 560);
    for (const auto& factory : factories) sections_.push_back(factory(this));
    backend_ = new SettingsBackend(ctx, this, sections_);
    connect(backend_, &SettingsBackend::saveRequested, this, &SettingsPanel::save);
    connect(backend_, &SettingsBackend::cancelRequested, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml_->setAttribute(Qt::WA_AlwaysStackOnTop, false);
    qml_->setClearColor(paletteColor(u"ELEVATE_2"));
    qml_->rootContext()->setContextProperty(QStringLiteral("settings"), backend_);
    qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/Settings.qml")));
    layout->addWidget(qml_);
    // A QML click handler must finish before its root object is destroyed.
    connect(this, &QDialog::finished, this, [this] { qml_->setSource(QUrl{}); }, Qt::QueuedConnection);
}
SettingsPanel::SettingsPanel(AppContext& app, QQmlEngine* engine, QWidget* parent)
    : SettingsPanel(app.coreContext(), engine, parent, app.settingsSections()) {}
SettingsPanel::~SettingsPanel() { qml_->setSource(QUrl{}); }
void SettingsPanel::save(const QVariantMap& values) {
    auto updated = config_;
    const auto text = [&](const char* key) { return values.value(QLatin1String(key)).toString().trimmed().toStdString(); };
    updated.paths.game = text("game");
    updated.paths.local = text("local");
    updated.paths.workshop = text("workshop");
    updated.paths.config_folder = text("config");
    updated.sort.use_alternative_package_ids = values.value("useAltIds").toBool();
    updated.sort.check_missing_dependencies = values.value("checkMissing").toBool();
    updated.sort.use_community_rules = values.value("useCommunity").toBool();
    updated.compact_mod_list = values.value("compact").toBool();
    updated.launch_args = text("launchArgs");
    updated.launch_wrapper = text("launchWrapper");
    updated.after_launch = static_cast<core::AfterLaunch>(values.value("afterLaunch").toInt());
    updated.launch_confirm_errors = values.value("confirmErrors").toBool();
    updated.launch_confirm_unsaved = values.value("confirmUnsaved").toBool();
    updated.launch_confirm_running = values.value("confirmRunning").toBool();
    for (auto* section : sections_) {
        const auto result = section->apply();
        if (!result) { backend_->reportSaveError(result.error().message); return; }
    }
    config_ = std::move(updated);
    accept();
}
} // namespace px::ui
