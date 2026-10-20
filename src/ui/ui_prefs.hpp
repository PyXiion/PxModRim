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

inline constexpr int clampUiScale(int percent) {
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

struct ResolvedScale {
  double scale = 100;
  QString source = QStringLiteral("saved");
  QString qtScaleFactor;
};

inline std::optional<int> parseScaleOverride(const QString& value) {
  QStringView digits(value);
  digits = digits.trimmed();
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

inline QString uiScaleArgument(const QStringList& arguments) {
  QString value;
  for (qsizetype index = 1; index < arguments.size(); ++index) {
    const auto& argument = arguments[index];
    if (argument == QStringLiteral("--")) break;
    if (argument.startsWith(QStringLiteral("--ui-scale="))) {
      value = argument.sliced(11);
      if (value.isNull()) value = QStringLiteral("");
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

inline ResolvedScale resolveUiScale(int saved, const QString& cliValue = {},
                                    const QString& envValue = {},
                                    const QString& qtScaleFactor = {}) {
  if (!qtScaleFactor.isNull()) {
    bool ok = false;
    const double factor = qtScaleFactor.toDouble(&ok);
    const double scale =
        ok && factor > 0 && std::isfinite(factor * 100) ? factor * 100 : 100;
    return {scale, QStringLiteral("qt-env"), qtScaleFactor};
  }
  if (const auto scale = parseScaleOverride(cliValue); scale) {
    return {static_cast<double>(*scale), QStringLiteral("cli"), {}};
  }
  if (const auto scale = parseScaleOverride(envValue); scale) {
    return {static_cast<double>(*scale), QStringLiteral("env"), {}};
  }
  return {
      static_cast<double>(clampUiScale(saved)), QStringLiteral("saved"), {}};
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
