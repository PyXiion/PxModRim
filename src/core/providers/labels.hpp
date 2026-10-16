#pragma once

#include <QChar>
#include <QLatin1StringView>
#include <QString>

namespace px::modrim {

// Display name for a mod provider, mirroring the legacy PROVIDER_LABELS table. Unknown ids
// fall back to the id with underscores as spaces and each word title-cased, as Python's
// str.title() does: a letter starts a new word only after an uncased character.
inline QString providerLabel(const QString& providerId) {
    if (providerId == QLatin1StringView("local")) {
        return QStringLiteral("Local");
    }
    if (providerId == QLatin1StringView("steam") || providerId == QLatin1StringView("downloaded")) {
        return QStringLiteral("Steam Workshop");
    }
    if (providerId == QLatin1StringView("core")) {
        return QStringLiteral("System / Core");
    }

    QString label = providerId;
    label.replace(u'_', u' ');
    bool afterLetter = false;
    for (QChar& c : label) {
        if (c.isLetter()) {
            c = afterLetter ? c.toLower() : c.toUpper();
            afterLetter = true;
        } else {
            afterLetter = false;
        }
    }
    return label;
}

}  // namespace px::modrim
