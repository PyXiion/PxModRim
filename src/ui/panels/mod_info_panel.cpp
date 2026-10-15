#include "ui/panels/mod_info_panel.h"
#include "ui/panels/mod_info_data.h"
#include "ui/panels/time_analytics_panel.h"
#include "ui/components/procedural_preview.h"
#include "ui/config.h"
#include "ui/models/impact.h"
#include "ui/theme/constants.h"
#include "ui/theme/palette.h"
#include "core/downloads/manager.h"
#include "core/mod_service.h"
#include "core/services/startup_impact_service/labels.h"
#include <QCoro/QCoroFuture>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QLinearGradient>
#include <QGuiApplication>
#include <QLabel>
#include <QPainter>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace px::ui {
class ModInfoBanner final : public QWidget {
public:
    explicit ModInfoBanner(QWidget* parent) : QWidget(parent) { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return std::min(width * BANNER_ASPECT_H / BANNER_ASPECT_W, BANNER_MAX_HEIGHT); }
    QSize sizeHint() const override { const int w = width() ? width() : 300; return {w, heightForWidth(w)}; }
    void setTitle(QString title, QString subtitle) { title_ = std::move(title); subtitle_ = std::move(subtitle); update(); }
    void setImage(QImage image, bool overlay) { image_ = std::move(image); overlay_ = overlay; rescale(); update(); }
protected:
    void resizeEvent(QResizeEvent* event) override { rescale(); QWidget::resizeEvent(event); }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setClipRect(rect());
        if (scaled_.isNull()) painter.fillRect(rect(), panelBgQ());
        else painter.drawImage((width() - scaled_.width()) / 2, (height() - scaled_.height()) / 2, scaled_);
        if (overlay_ && !title_.isEmpty()) {
            QLinearGradient gradient(0, height() - BANNER_OVERLAY_HEIGHT, 0, height());
            gradient.setColorAt(0, QColor(0, 0, 0, 0)); gradient.setColorAt(1, QColor(0, 0, 0, 180));
            painter.fillRect(0, height() - BANNER_OVERLAY_HEIGHT, width(), BANNER_OVERLAY_HEIGHT, gradient);
            const int top = height() - BANNER_OVERLAY_HEIGHT + 12;
            int title_height = BANNER_OVERLAY_HEIGHT - 24;
            if (!subtitle_.isEmpty()) title_height = title_height * 3 / 5;
            auto font = painter.font(); font.setPointSize(18); font.setBold(true);
            painter.setFont(font); painter.setPen(textMainQ());
            painter.drawText(QRect(16, top, width() - 32, title_height), Qt::AlignLeft | Qt::AlignBottom, title_);
            if (!subtitle_.isEmpty()) {
                font.setPointSize(12); font.setBold(false); painter.setFont(font); painter.setPen(textMutedQ());
                painter.drawText(QRect(16, top + title_height + 2, width() - 32, BANNER_OVERLAY_HEIGHT - title_height - 26), Qt::AlignLeft | Qt::AlignTop, subtitle_);
            }
        }
    }
private:
    void rescale() {
        scaled_ = image_.isNull() || width() <= 0 ? QImage{} : image_.scaled(width(), heightForWidth(width()), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    QImage image_, scaled_;
    QString title_, subtitle_;
    bool overlay_ = true;
};
namespace {
QString first_sentence(QString text) {
    static const QRegularExpression markup(R"(\[img\].*?\[/img\]|<[^>]+>|\[/?\w+(?:=[^\]]*)?\]|\[\*\])", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
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
}
ModInfoPanel::ModInfoPanel(core::CoreContext& ctx, QQmlEngine* engine, UiPrefs& prefs, QWidget* parent)
    : QWidget(parent), ctx_(ctx), engine_(engine), prefs_(prefs), banner_(new ModInfoBanner(this)),
      placeholder_(new QLabel("Select a mod to view details", this)), qml_(engine ? new QQuickWidget(engine, this) : new QQuickWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    banner_->hide(); layout->addWidget(banner_, 0, Qt::AlignTop);
    placeholder_->setAlignment(Qt::AlignCenter); placeholder_->setObjectName("placeholder");
    layout->addWidget(placeholder_, 0, Qt::AlignTop);
    qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml_->setClearColor(paletteColor(u"ELEVATE_2"));
    qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/ModInfo.qml")));
    qml_->hide(); layout->addWidget(qml_, 1);
    if (auto* root = qml_->rootObject()) {
        root->setProperty("descExpanded", prefs_.desc_expanded);
        connect(root, SIGNAL(openFolder()), this, SLOT(openFolder()));
        connect(root, SIGNAL(openUrl()), this, SLOT(openUrl()));
        connect(root, SIGNAL(selectMod(QString)), this, SIGNAL(modRequested(QString)));
        connect(root, SIGNAL(copyText(QString)), this, SLOT(copyText(QString)));
        connect(root, SIGNAL(openStartupDetails()), this, SLOT(openStartupDetails()));
        connect(root, SIGNAL(descToggled(bool)), this, SLOT(descToggled(bool)));
        connect(root, SIGNAL(updateMods()), this, SLOT(updateMod()));
    }
    setQml("downloadsBusy", ctx_.downloads().is_downloading());
    busyConnection_ = ctx_.downloads().busy_changed.connect([this](bool busy) { setQml("downloadsBusy", busy); syncUpdating(); });
}
ModInfoPanel::~ModInfoPanel() { ctx_.downloads().busy_changed.disconnect(busyConnection_); }
void ModInfoPanel::setQml(const char* name, const QVariant& value) { if (auto* root = qml_->rootObject()) root->setProperty(name, value); }
void ModInfoPanel::showMod(const core::ListedMod& mod, const QList<core::view::ModIssueView>& issues) {
    const auto token = ++previewToken_;
    mod_ = ctx_.allMods().value(mod.uuid());
    if (!mod_) {
        if (const auto* about = dynamic_cast<const core::AboutXmlMod*>(&mod)) mod_ = std::make_shared<core::AboutXmlMod>(*about);
        else mod_ = std::make_shared<core::ListedMod>(mod);
    }
    placeholder_->hide(); banner_->show(); qml_->show();
    const auto* about = dynamic_cast<const core::AboutXmlMod*>(&mod);
    banner_->setTitle(mod.name, !mod.description.isEmpty() ? first_sentence(mod.description) : about ? about->package_id.str() : QString{});
    ++startupToken_;
    setQml("startup", QVariant{}); setIssues(issues); syncUpdating();
    if (auto* root = qml_->rootObject()) QMetaObject::invokeMethod(root, "resetScroll");
    loadPreview(mod.mod_path(), mod.name, token);
}
void ModInfoPanel::setIssues(const QList<core::view::ModIssueView>& issues) {
    if (!mod_) return;
    auto data = build_mod_info(ctx_, *mod_, issues);
    const QString description = data.take("description").toString();
    setQml("info", data);
    if (description != description_) { description_ = description; setQml("description", description); }
}
void ModInfoPanel::clear() {
    mod_.reset(); ++previewToken_; ++startupToken_;
    placeholder_->show(); banner_->hide(); qml_->hide();
    description_.clear(); startupPid_.reset(); startupActivePids_.clear();
    setQml("info", QVariant{}); setQml("description", QString{}); setQml("startup", QVariant{});
}
void ModInfoPanel::syncUpdating() {
    const auto id = mod_ ? ctx_.downloads().updatable_id(*mod_) : std::nullopt;
    setQml("updatingThis", id && ctx_.downloads().active_ids().contains(*id));
}
void ModInfoPanel::openFolder() { if (mod_ && mod_->mod_path()) QDesktopServices::openUrl(QUrl::fromLocalFile(*mod_->mod_path())); }
void ModInfoPanel::openUrl() {
    const auto* about = dynamic_cast<const core::AboutXmlMod*>(mod_.get());
    if (!about) return;
    const QUrl url(about->url);
    const auto scheme = url.scheme().toLower();
    if (scheme == "http" || scheme == "https") QDesktopServices::openUrl(url);
}
void ModInfoPanel::copyText(const QString& text) { QGuiApplication::clipboard()->setText(text); }
void ModInfoPanel::descToggled(bool expanded) { prefs_.desc_expanded = expanded; if (ctx_.hasConfigService()) saveUiPrefs(prefs_, ctx_.configService()); }
void ModInfoPanel::updateMod() {
    if (!mod_) return;
    if (auto id = ctx_.downloads().updatable_id(*mod_)) performUpdate(*id);
}
QCoro::Task<void> ModInfoPanel::performUpdate(QString id) {
    const auto result = co_await ctx_.downloads().download_mods({id});
    if (!result) qWarning().noquote() << "Mod update not started:" << result.error().message;
}
QCoro::Task<void> ModInfoPanel::loadPreview(std::optional<QString> path, QString title, quint64 token) {
    QPointer<ModInfoPanel> guard(this);
    const int width = banner_->width() ? banner_->width() : 300;
    const int height = banner_->heightForWidth(width);
    auto future = QtConcurrent::run([path = std::move(path), title = std::move(title), width, height] {
        QImage image;
        if (path) image.load(*path + "/About/Preview.png");
        const bool overlay = image.isNull();
        if (overlay) image = generate_preview(title, width, height);
        return std::pair{image, overlay};
    });
    auto [image, overlay] = co_await future;
    if (guard && token == guard->previewToken_) guard->banner_->setImage(std::move(image), overlay);
}
QCoro::Task<void> ModInfoPanel::setTimeAnalytics(const std::optional<QString>& pid, const QStringList& activePids) {
    const auto token = ++startupToken_;
    startupPid_ = pid; startupActivePids_ = activePids;
    if (!pid) { setQml("startup", QVariantMap{{"available", false}, {"message", "Startup impact is unavailable."}}); co_return; }
    QPointer<ModInfoPanel> guard(this);
    const auto selectedPid = *pid;
    const auto active = activePids;
    auto result = co_await ctx_.modService().startup_impact().snapshot(active, selectedPid);
    if (!guard || token != guard->startupToken_) co_return;
    if (!result || !result->report) {
        guard->setQml("startup", QVariantMap{{"available", false}, {"message", result ? QStringLiteral("No startup data recorded yet. Launch the game to generate it.") : result.error().message}});
        co_return;
    }
    const auto& report = *result->report;
    const auto* mod = report.find(selectedPid, std::nullopt);
    const double own = result->selected_avg;
    if (!mod || own < .001) { guard->setQml("startup", QVariantMap{{"available", false}, {"message", "This mod wasn't measured in the last recorded launch."}}); co_return; }
    int rank = 1;
    for (const auto& entry : report.mods) rank += entry.total_impact_s > own;
    QList<std::pair<QString, double>> metrics;
    for (auto it = mod->metrics.cbegin(); it != mod->metrics.cend(); ++it) metrics.append({it.key(), it.value()});
    std::stable_sort(metrics.begin(), metrics.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    QVariantList rows;
    for (qsizetype i = 0; i < std::min<qsizetype>(3, metrics.size()); ++i) if (metrics[i].second >= .001)
        rows.append(QVariantMap{{"label", core::startup_impact::metric_label(metrics[i].first)}, {"value", format_duration(metrics[i].second)}, {"fraction", std::min(1.0, metrics[i].second / own)}});
    guard->setQml("startup", QVariantMap{{"available", true}, {"own", format_duration(own)}, {"rank", QString("#%1 slowest of %2 mods").arg(rank).arg(report.mods.size())},
        {"estimated", !active.contains(selectedPid, Qt::CaseInsensitive)}, {"color", impact_color(own)}, {"rows", rows}});
}
void ModInfoPanel::openStartupDetails() { showStartupDetails(); }
QCoro::Task<void> ModInfoPanel::showStartupDetails() {
    if (!startupDialog_) startupDialog_ = new StartupImpactDialog(&ctx_.modService().startup_impact(), engine_, this);
    startupDialog_->show(); startupDialog_->raise(); startupDialog_->activateWindow();
    co_await startupDialog_->panel->setData(startupPid_, startupActivePids_);
}
}
