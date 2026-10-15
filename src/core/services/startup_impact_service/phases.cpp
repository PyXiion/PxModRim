#include "core/services/startup_impact_service/phases.h"

#include <array>
#include <utility>

#include <QHash>
#include <QLatin1StringView>

#include "core/services/startup_impact_service/labels.h"

namespace px::core::startup_impact {

namespace {

const QHash<QString, Phase>& exact_phases() {
    static const QHash<QString, Phase> phases{
        {QStringLiteral("GlobalTextureAtlasManagerBakeStaticAtlases"), Phase::Content},
        {QStringLiteral("ModConstructor"), Phase::ModConstructors},
        {QStringLiteral("StaticConstructorOnStartupUtilityCallAll"), Phase::StaticConstructors},
        {QStringLiteral("ExecuteToExecuteWhenFinished"), Phase::Deferred},
        {QStringLiteral("LoadDefs"), Phase::Defs},
        {QStringLiteral("LoadModXml"), Phase::Defs},
        {QStringLiteral("CombineXml"), Phase::Defs},
        {QStringLiteral("LoadedModManagerParseAndProcessXML"), Phase::Defs},
        {QStringLiteral("RegisterXmlInheritance"), Phase::Defs},
        {QStringLiteral("ResolveXmlInheritance"), Phase::Defs},
        {QStringLiteral("ClearCachedXmlInheritance"), Phase::Defs},
        {QStringLiteral("DefDatabaseAddAllInMods"), Phase::Defs},
        {QStringLiteral("ResolveReferences"), Phase::Defs},
        {QStringLiteral("ErrorCheckAllDefs"), Phase::Defs},
        {QStringLiteral("ShortHashGiverGiveAllShortHashes"), Phase::Defs},
    };
    return phases;
}

// Checked in order after the exact names; the first matching prefix wins.
constexpr std::array<std::pair<QLatin1StringView, Phase>, 4> kPrefixPhases{{
    {QLatin1StringView{"ModContentPackReloadContentInt"}, Phase::Content},
    {QLatin1StringView{"ResolveAllWantedCrossReferences"}, Phase::Defs},
    {QLatin1StringView{"DefGeneratorGenerateImpliedDefs"}, Phase::Defs},
    {QLatin1StringView{"DefOfHelperRebindAllDefOfs"}, Phase::Defs},
}};

}  // namespace

QString phase_label(Phase phase) {
    switch (phase) {
        case Phase::Content:
            return QStringLiteral("Textures & audio");
        case Phase::ModConstructors:
            return QStringLiteral("Mod constructors");
        case Phase::StaticConstructors:
            return QStringLiteral("Static constructors");
        case Phase::Patches:
            return QStringLiteral("Patches");
        case Phase::Defs:
            return QStringLiteral("Defs & XML");
        case Phase::Deferred:
            return QStringLiteral("Deferred init");
        case Phase::Other:
            return QStringLiteral("Other");
    }
    return QString();
}

Phase metric_phase(const QString& key) {
    const QString stripped = strip_metric_prefix(key);
    const qsizetype bar = stripped.indexOf(u'|');
    const QString name = bar < 0 ? stripped : stripped.left(bar);

    if (const auto found = exact_phases().constFind(name); found != exact_phases().cend()) {
        return *found;
    }
    if (name.endsWith(QLatin1StringView{"Patches"})) {
        return Phase::Patches;
    }
    for (const auto& [prefix, phase] : kPrefixPhases) {
        if (name.startsWith(prefix)) {
            return phase;
        }
    }
    return Phase::Other;
}

PhaseTotals phase_totals(const QMap<QString, double>& metrics) {
    PhaseTotals totals{};
    for (auto it = metrics.cbegin(); it != metrics.cend(); ++it) {
        totals[phase_index(metric_phase(it.key()))] += it.value();
    }
    return totals;
}

}  // namespace px::core::startup_impact
