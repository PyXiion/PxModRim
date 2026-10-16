#pragma once

#include <optional>

#include <QCoro/QCoroTask>

#include "core/config.hpp"
#include "core/error.hpp"
#include "ui/ui_prefs.hpp"

namespace px::modrim {

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
    ConfigService& svc_;
    std::optional<UiPrefs> prefs_;
};

}  // namespace px::modrim
