#pragma once

#include <cstdint>

#include <QSet>
#include <QString>

namespace px::core::view {

// Display name of a mod provider; an unknown provider is shown by its id.
QString provider_label(const QString& provider_id);

// A sidebar filter and the mods it shows. `count` is derived from `visible_uuids`.
struct SidebarEntry {
    enum class Kind : std::uint8_t { All, Active, Inactive, WithErrors, WithWarnings, Provider };

    Kind kind = Kind::All;
    QString label;
    QSet<QString> visible_uuids;
    // Only for Kind::Provider: two providers can share a label, so the id tells them apart.
    QString provider_id;

    int count() const noexcept { return static_cast<int>(visible_uuids.size()); }
};

}  // namespace px::core::view
