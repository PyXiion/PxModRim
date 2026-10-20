#pragma once

#include <optional>

#include <QCoro/QCoroTask>

#include "core/config.hpp"
#include "core/error.hpp"
#include "ui/ui_prefs.hpp"

namespace px::modrim {

// The saved scale, read at startup before Qt and logging exist, so it never
// migrates or logs; an unreadable file gives 100.
int loadUiScale(const ConfigService& svc);

// Reads ui_prefs.json. An invalid ui_scale loads as 100 and a valid one is
// clamped, without discarding the other prefs. Fails only when the file cannot
// be migrated.
Result<UiPrefs> loadUiPrefs(const ConfigService& svc);

// Writes ui_prefs.json. Other UI code calls this after changing prefs.
Result<void> saveUiPrefs(const UiPrefs& prefs, const ConfigService& svc);

// Owns the UiPrefs lifecycle: loaded once in setup(), saved on request. The
// ConfigService must outlive this object.
class UiPrefsService {
 public:
  explicit UiPrefsService(ConfigService& svc);

  QCoro::Task<Result<void>> setup();

  // Valid only after a successful setup().
  const UiPrefs& prefs() const;

  Result<void> savePrefs() const;

 private:
  ConfigService& m_svc;
  std::optional<UiPrefs> m_prefs;
};

}  // namespace px::modrim
