#pragma once

#include <QSet>
#include <QString>
#include <cstdint>

namespace px::modrim::view {

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

  int count() const noexcept { return static_cast<int>(visibleUuids.size()); }
};

}  // namespace px::modrim::view
