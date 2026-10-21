#pragma once

#include <QLatin1StringView>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <array>

namespace px::modrim {

inline constexpr QLatin1StringView kRimworldSteamAppId{"294100"};
inline constexpr QLatin1StringView kDefaultMissingPackageid{
    "missing.packageid"};

enum class LaunchStrategy : int { kDirect = 0, kSteam = 1 };
enum class AfterLaunch : int { kStay = 0, kMinimize = 1, kClose = 2 };

struct RimWorldDlc {
  QLatin1StringView appId;
  QLatin1StringView packageId;
  QLatin1StringView name;
  QLatin1StringView steamUrl;
  QLatin1StringView description;
};

inline constexpr std::array<RimWorldDlc, 6> kRimworldDlcMetadata{{
    {QLatin1StringView{"294100"}, QLatin1StringView{"ludeon.rimworld"},
     QLatin1StringView{"RimWorld"},
     QLatin1StringView{"https://store.steampowered.com/app/294100/RimWorld"},
     QLatin1StringView{"Base game"}},
    {QLatin1StringView{"1149640"}, QLatin1StringView{"ludeon.rimworld.royalty"},
     QLatin1StringView{"RimWorld - Royalty"},
     QLatin1StringView{
         "https://store.steampowered.com/app/1149640/RimWorld__Royalty"},
     QLatin1StringView{"DLC #1"}},
    {QLatin1StringView{"1392840"},
     QLatin1StringView{"ludeon.rimworld.ideology"},
     QLatin1StringView{"RimWorld - Ideology"},
     QLatin1StringView{
         "https://store.steampowered.com/app/1392840/RimWorld__Ideology"},
     QLatin1StringView{"DLC #2"}},
    {QLatin1StringView{"1826140"}, QLatin1StringView{"ludeon.rimworld.biotech"},
     QLatin1StringView{"RimWorld - Biotech"},
     QLatin1StringView{
         "https://store.steampowered.com/app/1826140/RimWorld__Biotech"},
     QLatin1StringView{"DLC #3"}},
    {QLatin1StringView{"2380740"}, QLatin1StringView{"ludeon.rimworld.anomaly"},
     QLatin1StringView{"RimWorld - Anomaly"},
     QLatin1StringView{
         "https://store.steampowered.com/app/2380740/RimWorld__Anomaly"},
     QLatin1StringView{"DLC #4"}},
    {QLatin1StringView{"3022790"}, QLatin1StringView{"ludeon.rimworld.odyssey"},
     QLatin1StringView{"RimWorld - Odyssey"},
     QLatin1StringView{
         "https://store.steampowered.com/app/3022790/RimWorld__Odyssey"},
     QLatin1StringView{"DLC #5"}},
}};

inline const RimWorldDlc* findRimWorldDlcByPackageId(QStringView packageId) {
  for (const auto& release : kRimworldDlcMetadata) {
    if (packageId == release.packageId) return &release;
  }
  return nullptr;
}

}  // namespace px::modrim
