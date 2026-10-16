#include "core/services/startup_impact_service/labels.h"

#include <QHash>
#include <QLatin1StringView>

namespace px::modrim::startup_impact {

namespace {

constexpr QLatin1StringView kPrefix{"LoadingProgress.StartupImpact."};

const QHash<QString, QString>& metric_labels() {
    static const QHash<QString, QString> labels{
        {QStringLiteral("ModConstructor"), QStringLiteral("Running mod constructor")},
        {QStringLiteral("LoadDefs"), QStringLiteral("Loading defs")},
        {QStringLiteral("CombineXml"), QStringLiteral("Combining XML into a single XML document")},
        {QStringLiteral("TKeySystemParse"), QStringLiteral("Translation key system parsing")},
        {QStringLiteral("LoadedModManagerParseAndProcessXML"),
         QStringLiteral("Parsing and processing XML")},
        {QStringLiteral("ErrorCheckPatches"), QStringLiteral("Error checking patches")},
        {QStringLiteral("LoadPatches"), QStringLiteral("Loading patches")},
        {QStringLiteral("ApplyPatches"), QStringLiteral("Applying patches")},
        {QStringLiteral("RegisterXmlInheritance"), QStringLiteral("Registering XML inheritance")},
        {QStringLiteral("ResolveXmlInheritance"), QStringLiteral("Resolving XML inheritance")},
        {QStringLiteral("ClearCachedPatches"), QStringLiteral("Clearing cached patches")},
        {QStringLiteral("ClearCachedXmlInheritance"),
         QStringLiteral("Clearing cached XML inheritance")},
        {QStringLiteral("LanguageDatabaseInitAllMetadata"),
         QStringLiteral("Loading language metadata")},
        {QStringLiteral("DefDatabaseAddAllInMods"),
         QStringLiteral("Copying all Defs from mods to global databases")},
        {QStringLiteral("ResolveAllWantedCrossReferences"),
         QStringLiteral("Resolving cross-references")},
        {QStringLiteral("ResolveAllWantedCrossReferences.NonImplied"),
         QStringLiteral("Resolving cross-references between non-implied Defs")},
        {QStringLiteral("DefOfHelperRebindAllDefOfs"), QStringLiteral("Rebinding DefOfs")},
        {QStringLiteral("DefOfHelperRebindAllDefOfs.Early"),
         QStringLiteral("Rebinding DefOfs (early)")},
        {QStringLiteral("DefOfHelperRebindAllDefOfs.Final"),
         QStringLiteral("Rebinding DefOfs (final)")},
        {QStringLiteral("TKeySystemBuildMappings"), QStringLiteral("Translation key system mapping")},
        {QStringLiteral("BackStoryTranslationUtilityLoadAndInjectBackstoryData"),
         QStringLiteral("Loading and injecting legacy backstory translations")},
        {QStringLiteral("ModContentPackReloadContentInt"), QStringLiteral("Loading content")},
        {QStringLiteral("ModContentPackReloadContentInt.AudioClips"),
         QStringLiteral("Loading audio clips")},
        {QStringLiteral("ModContentPackReloadContentInt.Textures"),
         QStringLiteral("Loading textures")},
        {QStringLiteral("ModContentPackReloadContentInt.Strings"), QStringLiteral("Loading strings")},
        {QStringLiteral("ModContentPackReloadContentInt.AssetBundles"),
         QStringLiteral("Loading asset bundles")},
        {QStringLiteral("LoadedLanguageInjectIntoDataBeforeImpliedDefs"),
         QStringLiteral("Injecting language data (early pass)")},
        {QStringLiteral("ColoredTextResetStaticData"),
         QStringLiteral("Running global operations (early pass)")},
        {QStringLiteral("DefGeneratorGenerateImpliedDefsPreResolve"),
         QStringLiteral("Generating implied Defs (pre-resolve)")},
        {QStringLiteral("ResolveAllWantedCrossReferences.Implied"),
         QStringLiteral("Resolving cross-references between implied Defs")},
        {QStringLiteral("PlayDataLoaderResetStaticDataPre"),
         QStringLiteral("Global operations (pre-resolve)")},
        {QStringLiteral("ResolveReferences"), QStringLiteral("Resolving references")},
        {QStringLiteral("DefGeneratorGenerateImpliedDefsPostResolve"),
         QStringLiteral("Generating implied Defs (post-resolve)")},
        {QStringLiteral("PlayDataLoaderResetStaticDataPost"),
         QStringLiteral("Global operations (post-resolve)")},
        {QStringLiteral("ErrorCheckAllDefs"), QStringLiteral("Error checking all defs")},
        {QStringLiteral("KeyPrefsInit"), QStringLiteral("Loading keyboard preferences")},
        {QStringLiteral("ShortHashGiverGiveAllShortHashes"), QStringLiteral("Short hash giving")},
        {QStringLiteral("ExecuteToExecuteWhenFinished"),
         QStringLiteral("Running delayed initialization task")},
        {QStringLiteral("SolidBioDatabaseLoadAllBios"), QStringLiteral("Loading all bios")},
        {QStringLiteral("LoadedLanguageInjectIntoDataAfterImpliedDefs"),
         QStringLiteral("Injecting language data")},
        {QStringLiteral("StaticConstructorOnStartupUtilityCallAll"),
         QStringLiteral("Running static constructors")},
        {QStringLiteral("FloatMenuMakerMapInit"), QStringLiteral("Initializing float menu maker map")},
        {QStringLiteral("GlobalTextureAtlasManagerBakeStaticAtlases"),
         QStringLiteral("Texture atlas baking")},
        {QStringLiteral("AbstractFilesystemClearAllCache"),
         QStringLiteral("Clearing filesystem cache")},
        {QStringLiteral("LoadModXml"), QStringLiteral("Loading and parsing mod XML")},
    };
    return labels;
}

// The exact key, else its last dot-separated segment, else the key itself.
QString base_label(const QString& key) {
    const auto& labels = metric_labels();
    if (const auto found = labels.constFind(key); found != labels.cend()) {
        return *found;
    }
    const QString tail = key.mid(key.lastIndexOf(u'.') + 1);
    if (const auto found = labels.constFind(tail); found != labels.cend()) {
        return *found;
    }
    return key;
}

}  // namespace

QString strip_metric_prefix(const QString& key) {
    return key.startsWith(kPrefix) ? key.mid(kPrefix.size()) : key;
}

QString metric_label(const QString& key) {
    const QString stripped = strip_metric_prefix(key);
    const qsizetype bar = stripped.indexOf(u'|');
    const QString base = bar < 0 ? stripped : stripped.left(bar);
    const QString target = bar < 0 ? QString() : stripped.mid(bar + 1);
    const QString label = base_label(base);
    if (target.isEmpty()) {
        return label;
    }
    // Deferred tasks name the method they run after " -> "; only the target type is shown.
    const qsizetype arrow = target.indexOf(QStringLiteral(" -> "));
    const QString shown = arrow < 0 ? target : target.left(arrow);
    return QStringLiteral("%1 (%2)").arg(label, shown);
}

}  // namespace px::modrim::startup_impact
