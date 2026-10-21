#pragma once

#include <QLatin1StringView>
#include <QString>

namespace px::modrim {

// Display name for a mod provider; an unknown provider is shown by its id.
inline QString providerLabel(const QString& providerId) {
  if (providerId == QLatin1StringView("local")) {
    return QStringLiteral("Local");
  }
  if (providerId == QLatin1StringView("steam") ||
      providerId == QLatin1StringView("downloaded")) {
    return QStringLiteral("Steam Workshop");
  }
  if (providerId == QLatin1StringView("core")) {
    return QStringLiteral("System / Core");
  }
  return providerId;
}

}  // namespace px::modrim
