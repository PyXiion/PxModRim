#include "ui/models/impact.hpp"
#include "core/services/startup_impact_service/models.hpp"
#include "ui/theme/palette.hpp"
#include <cmath>
namespace px::modrim {
QString format_duration(double seconds) {
    if (seconds < 0.001) return QStringLiteral("0 ms");
    if (seconds < 1) return QStringLiteral("%1 ms").arg(static_cast<qint64>(std::nearbyint(seconds * 1000)));
    if (seconds < 60) return QStringLiteral("%1 s").arg(seconds, 0, 'f', 2);
    const auto rounded = static_cast<qint64>(std::nearbyint(seconds));
    return QStringLiteral("%1m %2s").arg(rounded / 60).arg(rounded % 60, 2, 10, QLatin1Char('0'));
}
QString impact_color(double seconds) {
    using namespace startup_impact;
    if (seconds < IMPACT_WARN_THRESHOLD_S) return paletteValue(u"SUCCESS");
    if (seconds < IMPACT_HIGH_THRESHOLD_S) return paletteValue(u"WARNING");
    return paletteValue(u"DANGER");
}
}
