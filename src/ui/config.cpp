#include "ui/config.hpp"

#include <QtGlobal>

namespace px::modrim {

namespace {

const QString kUiPrefsFile = QStringLiteral("ui_prefs.json");

}  // namespace

Result<void> saveUiPrefs(const UiPrefs& prefs, const ConfigService& svc) {
    return svc.save(kUiPrefsFile, prefs);
}

UiPrefsService::UiPrefsService(ConfigService& svc) : svc_(svc) {}

QCoro::Task<Result<void>> UiPrefsService::setup() {
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

Result<void> UiPrefsService::savePrefs() const {
    if (!prefs_) {
        return {};
    }
    return saveUiPrefs(*prefs_, svc_);
}

}  // namespace px::modrim
