#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include <glaze/glaze.hpp>

#include "core/constants.hpp"

namespace px::modrim {

constexpr int clampUiScale(int percent) {
  return std::clamp(percent, 100, 250);
}

inline int parseUiScale(const glz::json_t& value) {
  if (!value.is_number()) return 100;
  const double percent = value.get<double>();
  if (!std::isfinite(percent) || std::trunc(percent) != percent) return 100;
  return static_cast<int>(std::clamp(percent, 100.0, 250.0));
}

inline QByteArray uiScaleFactor(int percent) {
  return QByteArray::number(clampUiScale(percent) / 100.0, 'g', 3);
}

enum class ScaleSource { kSaved, kCli, kEnv, kQtEnv };

struct ResolvedScale {
  double scale = 100;
  ScaleSource source = ScaleSource::kSaved;
  // The user's QT_SCALE_FACTOR, set only when source is kQtEnv.
  QString qtScaleFactor;
};

inline std::optional<int> parseScaleOverride(const QString& value) {
  QStringView digits = QStringView(value).trimmed();
  if (digits.isEmpty()) return std::nullopt;
  const bool negative = digits.front() == u'-';
  if (negative || digits.front() == u'+') digits = digits.sliced(1);
  if (digits.isEmpty()) return std::nullopt;
  int percent = 0;
  for (const QChar digit : digits) {
    if (digit < u'0' || digit > u'9') return std::nullopt;
    percent = std::min(250, percent * 10 + digit.unicode() - u'0');
  }
  return negative ? 100 : clampUiScale(percent);
}

// The last --ui-scale value before "--": null when absent, empty when given
// without a value.
inline QString uiScaleArgument(const QStringList& arguments) {
  QString value;
  for (qsizetype index = 1; index < arguments.size(); ++index) {
    const auto& argument = arguments[index];
    if (argument == QStringLiteral("--")) break;
    if (argument.startsWith(QStringLiteral("--ui-scale="))) {
      value = argument.sliced(11);
    } else if (argument == QStringLiteral("--ui-scale")) {
      value = QStringLiteral("");
      if (index + 1 < arguments.size() &&
          !arguments[index + 1].startsWith(QStringLiteral("--"))) {
        value = arguments[++index];
      }
    }
  }
  return value;
}

// A set QT_SCALE_FACTOR wins, then a valid CLI value, then a valid
// PX_UI_SCALE, then the saved value. Null strings mean "not set".
inline ResolvedScale resolveUiScale(int saved, const QString& cliValue = {},
                                    const QString& envValue = {},
                                    const QString& qtScaleFactor = {}) {
  if (!qtScaleFactor.isNull()) {
    bool ok = false;
    const double percent = qtScaleFactor.toDouble(&ok) * 100;
    const bool usable = ok && percent > 0 && std::isfinite(percent);
    return {usable ? percent : 100, ScaleSource::kQtEnv, qtScaleFactor};
  }
  if (const auto scale = parseScaleOverride(cliValue)) {
    return {static_cast<double>(*scale), ScaleSource::kCli, {}};
  }
  if (const auto scale = parseScaleOverride(envValue)) {
    return {static_cast<double>(*scale), ScaleSource::kEnv, {}};
  }
  return {static_cast<double>(clampUiScale(saved)), ScaleSource::kSaved, {}};
}

// Persisted UI state (ui_prefs.json). Field names are the JSON keys.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct UiPrefs {
  bool desc_expanded = false;
  LaunchStrategy launch_strategy = LaunchStrategy::kDirect;
  std::string skipped_update_tag;
  bool rail_collapsed = false;
  int ui_scale = 100;

  bool operator==(const UiPrefs&) const = default;
};
// NOLINTEND(readability-identifier-naming)

}  // namespace px::modrim
