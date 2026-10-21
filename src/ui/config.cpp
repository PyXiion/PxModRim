#include "ui/config.hpp"

#include <QtGlobal>

#include <spdlog/spdlog.h>

namespace px::modrim {

namespace {

const QString kUiPrefsFile = QStringLiteral("ui_prefs.json");

}  // namespace

int loadUiScale(const ConfigService& svc) {
  const auto raw = svc.loadRaw(kUiPrefsFile);
  const auto found = raw.find("ui_scale");
  return found == raw.end() ? 100 : parseUiScale(found->second);
}

Result<UiPrefs> loadUiPrefs(const ConfigService& svc) {
  auto raw = svc.load<JsonObject>(kUiPrefsFile);
  if (!raw) {
    return std::unexpected(raw.error());
  }
  // An unusable scale must not fail the typed decode, which would replace
  // every other preference with its default.
  if (const auto found = raw->find("ui_scale"); found != raw->end()) {
    found->second = static_cast<double>(parseUiScale(found->second));
  }
  UiPrefs prefs{};
  const auto text = glz::write_json(*raw);
  if (!text) {
    return prefs;
  }
  if (auto error = glz::read<kJsonReadOpts>(prefs, *text)) {
    spdlog::warn("Failed to load {}: {}", kUiPrefsFile.toStdString(),
                 glz::format_error(error, *text));
    return UiPrefs{};
  }
  return prefs;
}

Result<void> saveUiPrefs(const UiPrefs& prefs, const ConfigService& svc) {
  return svc.save(kUiPrefsFile, prefs);
}

}  // namespace px::modrim
