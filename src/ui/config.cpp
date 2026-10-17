#include "ui/config.hpp"

#include <QtGlobal>

namespace px::modrim {

namespace {

const QString kUiPrefsFile = QStringLiteral("ui_prefs.json");

}  // namespace

Result<void> saveUiPrefs(const UiPrefs& prefs, const ConfigService& svc) {
  return svc.save(kUiPrefsFile, prefs);
}

UiPrefsService::UiPrefsService(ConfigService& svc) : m_svc(svc) {}

QCoro::Task<Result<void>> UiPrefsService::setup() {
  auto loaded = m_svc.load<UiPrefs>(kUiPrefsFile);
  if (!loaded) {
    co_return std::unexpected(loaded.error());
  }
  m_prefs = std::move(*loaded);
  co_return {};
}

const UiPrefs& UiPrefsService::prefs() const {
  Q_ASSERT(m_prefs.has_value());
  return *m_prefs;
}

Result<void> UiPrefsService::savePrefs() const {
  if (!m_prefs) {
    return {};
  }
  return saveUiPrefs(*m_prefs, m_svc);
}

}  // namespace px::modrim
