#include "core/sort/config.h"

#include <QString>

namespace px::core::sort {

TierConfig TierConfig::defaults() {
    return TierConfig{
        .tier_0 =
            {
                PackageId(QStringLiteral("zetrith.prepatcher")),
                PackageId(QStringLiteral("brrainz.harmony")),
                PackageId(QStringLiteral("brrainz.visualexceptions")),
                PackageId(QStringLiteral("ludeon.rimworld")),
                PackageId(QStringLiteral("ludeon.rimworld.royalty")),
                PackageId(QStringLiteral("ludeon.rimworld.ideology")),
                PackageId(QStringLiteral("ludeon.rimworld.biotech")),
                PackageId(QStringLiteral("ludeon.rimworld.anomaly")),
                PackageId(QStringLiteral("ludeon.rimworld.odyssey")),
            },
        .tier_1 =
            {
                PackageId(QStringLiteral("adaptive.storage.framework")),
                PackageId(QStringLiteral("aoba.framework")),
                PackageId(QStringLiteral("aoba.exosuit.framework")),
                PackageId(QStringLiteral("ebsg.framework")),
                PackageId(QStringLiteral("imranfish.xmlextensions")),
                PackageId(QStringLiteral("thesepeople.ritualattachableoutcomes")),
                PackageId(QStringLiteral("ohno.asf.ab.local")),
                PackageId(QStringLiteral("oskarpotocki.vanillafactionsexpanded.core")),
                PackageId(QStringLiteral("owlchemist.cherrypicker")),
                PackageId(QStringLiteral("redmattis.betterprerequisites")),
                PackageId(QStringLiteral("smashphil.vehicleframework")),
                PackageId(QStringLiteral("unlimitedhugs.hugslib")),
                PackageId(QStringLiteral("vanillaexpanded.backgrounds")),
            },
        .tier_3 = {},
    };
}

}  // namespace px::core::sort
