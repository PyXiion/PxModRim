#pragma once

#include <QSet>
#include <QString>
#include <cstdint>

namespace px::modrim::view {

// Display name of a mod provider; an unknown provider is shown by its id.
QString providerLabel(const QString& providerId);

// A sidebar filter and the mods it shows. `count` is derived from
// `visible_uuids`.
struct SidebarEntry {
  enum class Kind : std::uint8_t {
    kAll,
    kActive,
    kInactive,
    kWithErrors,
    kWithWarnings,
    kProvider
  };

  Kind kind = Kind::kAll;
  QString label;
  QSet<QString> visibleUuids;
  // Only for Kind::Provider: two providers can share a label, so the id tells
  // them apart.
  QString providerId;

  int count() const noexcept { return static_cast<int>(visibleUuids.size()); }
};

}  // namespace px::modrim::view
