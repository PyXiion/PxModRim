#include "ui/panels/time_analytics_panel.h"
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>
#include "ui/components/dialog_chrome.h"
#include "ui/panels/startup_impact_data.h"
#include "ui/theme/palette.h"
namespace px::ui {
TimeAnalyticsPanel::TimeAnalyticsPanel(core::startup_impact::StartupImpactService* service, QQmlEngine* engine, QWidget* parent)
    : QWidget(parent), service_(service), qml_(new QQuickWidget(engine, this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    qml_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qml_->setAttribute(Qt::WA_AlwaysStackOnTop, false);
    qml_->setClearColor(paletteColor(u"ELEVATE_1"));
    qml_->setSource(QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/TimeAnalytics.qml")));
    layout->addWidget(qml_);
}
QCoro::Task<void> TimeAnalyticsPanel::setData(std::optional<QString> pid, QStringList activePids) {
    const auto token = ++requestToken_;
    QPointer<QQuickItem> root = qml_->rootObject();
    if (!root) co_return;
    if (!service_) { root->setProperty("sourceData", QVariant{}); co_return; }
    QPointer<TimeAnalyticsPanel> guard(this);
    auto result = co_await service_->snapshot(activePids);
    if (!guard || !root || token != guard->requestToken_) co_return;
    if (!result || !result->report) { root->setProperty("sourceData", QVariant{}); co_return; }
    double estimated = result->base_game_avg;
    for (const auto& total : result->totals) estimated += total.on_thread + total.off_thread;
    root->setProperty("sourceData", build_startup_impact(*result->report, activePids, pid, estimated));
}
void TimeAnalyticsPanel::clear() {
    ++requestToken_;
    if (auto* root = qml_->rootObject()) root->setProperty("sourceData", QVariant{});
}
StartupImpactDialog::StartupImpactDialog(core::startup_impact::StartupImpactService* service, QQmlEngine* engine, QWidget* parent)
    : QDialog(parent), panel(new TimeAnalyticsPanel(service, engine, this)) {
    setWindowTitle(QStringLiteral("Startup impact"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(900, 720);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(panel);
    installDialogChrome(this);
}
} // namespace px::ui
