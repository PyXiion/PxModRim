#include "ui/components/header_controller.hpp"

#include <initializer_list>
#include <utility>

#include "core/constants.hpp"

namespace px::modrim {

namespace {

QString strategyLabel(LaunchStrategy strategy) {
  switch (strategy) {
    case LaunchStrategy::kDirect:
      return QStringLiteral("Direct executable");
    case LaunchStrategy::kSteam:
      return QStringLiteral("Steam");
  }
  return {};
}

}  // namespace

HeaderController::HeaderController(bool isFrameless, int initialStrategy,
                                   QString appVersion, QVariantMap tooltips,
                                   bool downloadsAvailable, QObject* parent)
    : QObject(parent),
      m_isFrameless(isFrameless),
      m_appVersion(std::move(appVersion)),
      m_tooltips(std::move(tooltips)),
      m_downloadsAvailable(downloadsAvailable),
      m_strategyIndex(initialStrategy) {}

QVariantList HeaderController::strategies() const {
  QVariantList list;
  for (const auto strategy :
       {LaunchStrategy::kDirect, LaunchStrategy::kSteam}) {
    list.append(
        QVariantMap{{QStringLiteral("index"), static_cast<int>(strategy)},
                    {QStringLiteral("label"), strategyLabel(strategy)}});
  }
  return list;
}

void HeaderController::setMaximized(bool value) {
  if (m_maximized != value) {
    m_maximized = value;
    emit maximizedChanged();
  }
}

void HeaderController::setUnsavedChanges(bool value) {
  if (m_unsavedChanges != value) {
    m_unsavedChanges = value;
    emit unsavedChangesChanged();
  }
}

void HeaderController::setLaunchState(const QString& state) {
  if (m_launchState != state) {
    m_launchState = state;
    emit launchStateChanged();
  }
}

void HeaderController::setTaskText(const QString& text) {
  if (m_taskText != text) {
    m_taskText = text;
    emit taskTextChanged();
  }
}

void HeaderController::setDownloadsBusy(bool busy) {
  if (m_downloadsBusy != busy) {
    m_downloadsBusy = busy;
    emit downloadsBusyChanged();
  }
}

void HeaderController::setDownloadsProgressShown(bool shown) {
  if (m_downloadsProgressShown != shown) {
    m_downloadsProgressShown = shown;
    emit downloadsProgressShownChanged();
  }
}

void HeaderController::setDownloadsProgress(const QString& text, int value,
                                            int total) {
  m_downloadsText = text;
  m_downloadsValue = value;
  m_downloadsTotal = total;
  emit downloadsProgressChanged();
}

void HeaderController::refresh() { emit refreshRequested(); }

void HeaderController::autoSort() { emit sortRequested(); }

void HeaderController::openDownloads() { emit downloadsRequested(); }

void HeaderController::updateMods() { emit updateModsRequested(); }

void HeaderController::save() { emit saveRequested(); }

void HeaderController::launch() { emit launchRequested(); }

void HeaderController::minimize() { emit minimizeRequested(); }

void HeaderController::maximize() { emit maximizeRequested(); }

void HeaderController::closeWindow() { emit closeRequested(); }

void HeaderController::setStrategy(int index) {
  if (m_strategyIndex != index) {
    m_strategyIndex = index;
    emit strategyChanged(index);
  }
}

void HeaderController::dragStarted() { emit dragStartRequested(); }

}  // namespace px::modrim
