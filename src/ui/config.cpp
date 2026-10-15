#include "ui/config.h"

#include <QtGlobal>

namespace px::ui {

namespace {

const QString kUiPrefsFile = QStringLiteral("ui_prefs.json");

}  // namespace

core::Result<void> saveUiPrefs(const UiPrefs& prefs, const core::ConfigService& svc) {
    return svc.save(kUiPrefsFile, prefs);
}

UiPrefsService::UiPrefsService(core::ConfigService& svc) : svc_(svc) {}

QCoro::Task<core::Result<void>> UiPrefsService::setup() {
    auto loaded = svc_.load<UiPrefs>(kUiPrefsFile);
    if (!loaded) {
        co_return std::unexpected(loaded.error());
    }
    prefs_ = std::move(*loaded);
    co_return {};
}

const UiPrefs& UiPrefsService::prefs() const {
    Q_ASSERT(prefs_.has_value());
    return *prefs_;
}

core::Result<void> UiPrefsService::savePrefs() const {
    if (!prefs_) {
        return {};
    }
    return saveUiPrefs(*prefs_, svc_);
}

}  // namespace px::ui
