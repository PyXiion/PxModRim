#pragma once

#include <array>

#include <QLatin1StringView>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace px::modrim {

inline constexpr QLatin1StringView RIMWORLD_STEAM_APP_ID{"294100"};

enum class LaunchStrategy : int {
    Direct = 0,
    Steam = 1,
};

enum class AfterLaunch : int {
    Stay = 0,
    Minimize = 1,
    Close = 2,
};

inline constexpr QLatin1StringView DEFAULT_MISSING_PACKAGEID{"missing.packageid"};

struct RimWorldDlc {
    QLatin1StringView appId;
    QLatin1StringView packageId;
    QLatin1StringView name;
    QLatin1StringView steamUrl;
    QLatin1StringView description;
};

// Ordered by Steam app id, matching the legacy dict's iteration order.
inline constexpr std::array<RimWorldDlc, 6> RIMWORLD_DLC_METADATA{{
    {QLatin1StringView{"294100"}, QLatin1StringView{"ludeon.rimworld"},
     QLatin1StringView{"RimWorld"},
     QLatin1StringView{"https://store.steampowered.com/app/294100/RimWorld"},
     QLatin1StringView{"Base game"}},
    {QLatin1StringView{"1149640"}, QLatin1StringView{"ludeon.rimworld.royalty"},
     QLatin1StringView{"RimWorld - Royalty"},
     QLatin1StringView{"https://store.steampowered.com/app/1149640/RimWorld__Royalty"},
     QLatin1StringView{"DLC #1"}},
    {QLatin1StringView{"1392840"}, QLatin1StringView{"ludeon.rimworld.ideology"},
     QLatin1StringView{"RimWorld - Ideology"},
     QLatin1StringView{"https://store.steampowered.com/app/1392840/RimWorld__Ideology"},
     QLatin1StringView{"DLC #2"}},
    {QLatin1StringView{"1826140"}, QLatin1StringView{"ludeon.rimworld.biotech"},
     QLatin1StringView{"RimWorld - Biotech"},
     QLatin1StringView{"https://store.steampowered.com/app/1826140/RimWorld__Biotech"},
     QLatin1StringView{"DLC #3"}},
    {QLatin1StringView{"2380740"}, QLatin1StringView{"ludeon.rimworld.anomaly"},
     QLatin1StringView{"RimWorld - Anomaly"},
     QLatin1StringView{"https://store.steampowered.com/app/2380740/RimWorld__Anomaly/"},
     QLatin1StringView{"DLC #4"}},
    {QLatin1StringView{"3022790"}, QLatin1StringView{"ludeon.rimworld.odyssey"},
     QLatin1StringView{"RimWorld - Odyssey"},
     QLatin1StringView{"https://store.steampowered.com/app/3022790/RimWorld__Odyssey/"},
     QLatin1StringView{"DLC #5"}},
}};

// Ordered package ids of RIMWORLD_DLC_METADATA.
inline const QStringList RIMWORLD_PACKAGE_IDS = [] {
    QStringList ids;
    for (const RimWorldDlc& dlc : RIMWORLD_DLC_METADATA) {
        ids.append(QString(dlc.packageId));
    }
    return ids;
}();

// Exact (case-sensitive) lookup; callers pass lowercase ids as produced by CaseInsensitiveStr.
inline const RimWorldDlc* findRimWorldDlcByPackageId(QStringView packageId) {
    for (const RimWorldDlc& dlc : RIMWORLD_DLC_METADATA) {
        if (packageId == QLatin1StringView(dlc.packageId)) {
            return &dlc;
        }
    }
    return nullptr;
}

}  // namespace px::modrim
