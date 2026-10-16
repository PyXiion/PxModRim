#include "core/models/view/sidebar.hpp"

#include <QHash>

namespace px::modrim::view {

QString providerLabel(const QString& providerId) {
    static const QHash<QString, QString> labels{
        {QStringLiteral("local"), QStringLiteral("Local")},
        {QStringLiteral("steam"), QStringLiteral("Steam Workshop")},
        {QStringLiteral("downloaded"), QStringLiteral("Steam Workshop")},
        {QStringLiteral("core"), QStringLiteral("System / Core")},
    };
    return labels.value(providerId, providerId);
}

}  // namespace px::modrim::view
