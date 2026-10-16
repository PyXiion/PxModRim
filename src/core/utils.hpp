#pragma once

#include <optional>

#include <QString>
#include <QStringView>

namespace px::modrim {

// Case-insensitive search for About/About.xml inside a mod directory.
std::optional<QString> findAboutXml(const QString& modPath);

// True for a non-empty string made only of ASCII digits (Python's str.isdigit for ASCII input).
bool isAsciiDigits(QStringView text);

}  // namespace px::modrim
