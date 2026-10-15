#pragma once

#include <optional>

#include <QCoro/QCoroTask>

#include "core/config.h"
#include "core/error.h"
#include "ui/ui_prefs.h"

namespace px::ui {

// Writes ui_prefs.json. Other UI code calls this after changing prefs.
core::Result<void> saveUiPrefs(const UiPrefs& prefs, const core::ConfigService& svc);

// Owns the UiPrefs lifecycle: loaded once in setup(), saved on request. The
// ConfigService must outlive this object.
class UiPrefsService {
public:
    explicit UiPrefsService(core::ConfigService& svc);

    QCoro::Task<core::Result<void>> setup();

    // Valid only after a successful setup().
    const UiPrefs& prefs() const;

    core::Result<void> savePrefs() const;

private:
    core::ConfigService& svc_;
    std::optional<UiPrefs> prefs_;
};

}  // namespace px::ui
