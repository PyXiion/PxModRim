#include "core/models/view/sidebar.h"

#include <QHash>

namespace px::core::view {

QString provider_label(const QString& provider_id) {
    static const QHash<QString, QString> labels{
        {QStringLiteral("local"), QStringLiteral("Local")},
        {QStringLiteral("steam"), QStringLiteral("Steam Workshop")},
        {QStringLiteral("downloaded"), QStringLiteral("Steam Workshop")},
        {QStringLiteral("core"), QStringLiteral("System / Core")},
    };
    return labels.value(provider_id, provider_id);
}

}  // namespace px::core::view
