#include "core/services/startup_impact_service/phases.hpp"

#include <QHash>
#include <QLatin1StringView>
#include <array>
#include <utility>

#include "core/services/startup_impact_service/labels.hpp"

namespace px::modrim::startup_impact {

namespace {

const QHash<QString, Phase>& exactPhases() {
  static const QHash<QString, Phase> phases{
      {QStringLiteral("GlobalTextureAtlasManagerBakeStaticAtlases"),
       Phase::kContent},
      {QStringLiteral("ModConstructor"), Phase::kModConstructors},
      {QStringLiteral("StaticConstructorOnStartupUtilityCallAll"),
       Phase::kStaticConstructors},
      {QStringLiteral("ExecuteToExecuteWhenFinished"), Phase::kDeferred},
      {QStringLiteral("LoadDefs"), Phase::kDefs},
      {QStringLiteral("LoadModXml"), Phase::kDefs},
      {QStringLiteral("CombineXml"), Phase::kDefs},
      {QStringLiteral("LoadedModManagerParseAndProcessXML"), Phase::kDefs},
      {QStringLiteral("RegisterXmlInheritance"), Phase::kDefs},
      {QStringLiteral("ResolveXmlInheritance"), Phase::kDefs},
      {QStringLiteral("ClearCachedXmlInheritance"), Phase::kDefs},
      {QStringLiteral("DefDatabaseAddAllInMods"), Phase::kDefs},
      {QStringLiteral("ResolveReferences"), Phase::kDefs},
      {QStringLiteral("ErrorCheckAllDefs"), Phase::kDefs},
      {QStringLiteral("ShortHashGiverGiveAllShortHashes"), Phase::kDefs},
  };
  return phases;
}

// Checked in order after the exact names; the first matching prefix wins.
constexpr std::array<std::pair<QLatin1StringView, Phase>, 4> kPrefixPhases{{
    {QLatin1StringView{"ModContentPackReloadContentInt"}, Phase::kContent},
    {QLatin1StringView{"ResolveAllWantedCrossReferences"}, Phase::kDefs},
    {QLatin1StringView{"DefGeneratorGenerateImpliedDefs"}, Phase::kDefs},
    {QLatin1StringView{"DefOfHelperRebindAllDefOfs"}, Phase::kDefs},
}};

}  // namespace

QString phaseLabel(Phase phase) {
  switch (phase) {
    case Phase::kContent:
      return QStringLiteral("Textures & audio");
    case Phase::kModConstructors:
      return QStringLiteral("Mod constructors");
    case Phase::kStaticConstructors:
      return QStringLiteral("Static constructors");
    case Phase::kPatches:
      return QStringLiteral("Patches");
    case Phase::kDefs:
      return QStringLiteral("Defs & XML");
    case Phase::kDeferred:
      return QStringLiteral("Deferred init");
    case Phase::kOther:
      return QStringLiteral("Other");
  }
  return QString();
}

Phase metricPhase(const QString& key) {
  const QString stripped = stripMetricPrefix(key);
  const qsizetype bar = stripped.indexOf(u'|');
  const QString name = bar < 0 ? stripped : stripped.left(bar);

  if (const auto found = exactPhases().constFind(name);
      found != exactPhases().cend()) {
    return *found;
  }
  if (name.endsWith(QLatin1StringView{"Patches"})) {
    return Phase::kPatches;
  }
  for (const auto& [prefix, phase] : kPrefixPhases) {
    if (name.startsWith(prefix)) {
      return phase;
    }
  }
  return Phase::kOther;
}

PhaseTotals phaseTotals(const QMap<QString, double>& metrics) {
  PhaseTotals totals{};
  for (auto it = metrics.cbegin(); it != metrics.cend(); ++it) {
    totals[phaseIndex(metricPhase(it.key()))] += it.value();
  }
  return totals;
}

}  // namespace px::modrim::startup_impact
