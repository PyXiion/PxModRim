#include "ui/panels/time_analytics_panel.hpp"

#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>

#include "ui/components/dialog_chrome.hpp"
#include "ui/panels/startup_impact_data.hpp"
#include "ui/theme/palette.hpp"
namespace px::modrim {
TimeAnalyticsPanel::TimeAnalyticsPanel(
    startup_impact::StartupImpactService* service, QQmlEngine* engine,
    QWidget* parent)
    : QWidget(parent),
      m_service(service),
      m_qml(new QQuickWidget(engine, this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  m_qml->setResizeMode(QQuickWidget::SizeRootObjectToView);
  m_qml->setAttribute(Qt::WA_AlwaysStackOnTop, false);
  m_qml->setClearColor(paletteColor(u"ELEVATE_1"));
  m_qml->setSource(
      QUrl(QStringLiteral("qrc:/qt/qml/PxModRim/TimeAnalytics.qml")));
  layout->addWidget(m_qml);
}
QCoro::Task<void> TimeAnalyticsPanel::setData(std::optional<QString> pid,
                                              QStringList activePids) {
  const auto token = ++m_requestToken;
  QPointer<QQuickItem> root = m_qml->rootObject();
  if (!root) co_return;
  if (!m_service) {
    root->setProperty("sourceData", QVariant{});
    co_return;
  }
  QPointer<TimeAnalyticsPanel> guard(this);
  auto result = co_await m_service->snapshot(activePids);
  if (!guard || !root || token != guard->m_requestToken) co_return;
  if (!result || !result->report) {
    root->setProperty("sourceData", QVariant{});
    co_return;
  }
  double estimated = result->baseGameAvg;
  for (const auto& total : result->totals)
    estimated += total.onThread + total.offThread;
  root->setProperty(
      "sourceData",
      buildStartupImpact(*result->report, activePids, pid, estimated));
}
void TimeAnalyticsPanel::clear() {
  ++m_requestToken;
  if (auto* root = m_qml->rootObject())
    root->setProperty("sourceData", QVariant{});
}
StartupImpactDialog::StartupImpactDialog(
    startup_impact::StartupImpactService* service, QQmlEngine* engine,
    QWidget* parent)
    : QDialog(parent), panel(new TimeAnalyticsPanel(service, engine, this)) {
  setWindowTitle(QStringLiteral("Startup impact"));
  setAttribute(Qt::WA_DeleteOnClose);
  resize(900, 720);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(panel);
  installDialogChrome(this);
}
}  // namespace px::modrim
