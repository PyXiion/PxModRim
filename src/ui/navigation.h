#pragma once

#include <optional>

#include <QLatin1StringView>
#include <QString>
#include <QStringList>

namespace px::modrim {

// modrim://<view_id>[/<part>…] links that open a view and optionally a sub-route inside it.
inline constexpr QLatin1StringView kRouteScheme{"modrim"};
inline constexpr QLatin1StringView kSettingsViewId{"settings"};

struct Route {
    QString view_id;
    QStringList path;

    QString url() const;

    bool operator==(const Route&) const = default;
};

// Each path part is percent-encoded with nothing left unescaped, so ids containing
// '/' or ':' round-trip.
QString buildRoute(const QString& viewId, const QStringList& path = {});

// Parses `modrim://<view>[/<part>…]`; anything else is not a route.
std::optional<Route> parseRoute(const QString& url);

}  // namespace px::modrim
