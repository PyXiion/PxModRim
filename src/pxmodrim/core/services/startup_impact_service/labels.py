from __future__ import annotations

METRIC_LABELS: dict[str, str] = {
    "ModConstructor": "Running mod constructor",
    "LoadDefs": "Loading defs",
    "CombineXml": "Combining XML into a single XML document",
    "TKeySystemParse": "Translation key system parsing",
    "LoadedModManagerParseAndProcessXML": "Parsing and processing XML",
    "ErrorCheckPatches": "Error checking patches",
    "LoadPatches": "Loading patches",
    "ApplyPatches": "Applying patches",
    "RegisterXmlInheritance": "Registering XML inheritance",
    "ResolveXmlInheritance": "Resolving XML inheritance",
    "ClearCachedPatches": "Clearing cached patches",
    "ClearCachedXmlInheritance": "Clearing cached XML inheritance",
    "LanguageDatabaseInitAllMetadata": "Loading language metadata",
    "DefDatabaseAddAllInMods": "Copying all Defs from mods to global databases",
    "ResolveAllWantedCrossReferences": "Resolving cross-references",
    "ResolveAllWantedCrossReferences.NonImplied": "Resolving cross-references between non-implied Defs",  # noqa: E501
    "DefOfHelperRebindAllDefOfs": "Rebinding DefOfs",
    "DefOfHelperRebindAllDefOfs.Early": "Rebinding DefOfs (early)",
    "DefOfHelperRebindAllDefOfs.Final": "Rebinding DefOfs (final)",
    "TKeySystemBuildMappings": "Translation key system mapping",
    "BackStoryTranslationUtilityLoadAndInjectBackstoryData": "Loading and injecting legacy backstory translations",  # noqa: E501
    "ModContentPackReloadContentInt": "Loading content",
    "ModContentPackReloadContentInt.AudioClips": "Loading audio clips",
    "ModContentPackReloadContentInt.Textures": "Loading textures",
    "ModContentPackReloadContentInt.Strings": "Loading strings",
    "ModContentPackReloadContentInt.AssetBundles": "Loading asset bundles",
    "LoadedLanguageInjectIntoDataBeforeImpliedDefs": "Injecting language data (early pass)",  # noqa: E501
    "ColoredTextResetStaticData": "Running global operations (early pass)",
    "DefGeneratorGenerateImpliedDefsPreResolve": "Generating implied Defs (pre-resolve)",  # noqa: E501
    "ResolveAllWantedCrossReferences.Implied": "Resolving cross-references between implied Defs",  # noqa: E501
    "PlayDataLoaderResetStaticDataPre": "Global operations (pre-resolve)",
    "ResolveReferences": "Resolving references",
    "DefGeneratorGenerateImpliedDefsPostResolve": "Generating implied Defs (post-resolve)",  # noqa: E501
    "PlayDataLoaderResetStaticDataPost": "Global operations (post-resolve)",
    "ErrorCheckAllDefs": "Error checking all defs",
    "KeyPrefsInit": "Loading keyboard preferences",
    "ShortHashGiverGiveAllShortHashes": "Short hash giving",
    "ExecuteToExecuteWhenFinished": "Running delayed initialization task",
    "SolidBioDatabaseLoadAllBios": "Loading all bios",
    "LoadedLanguageInjectIntoDataAfterImpliedDefs": "Injecting language data",
    "StaticConstructorOnStartupUtilityCallAll": "Running static constructors",
    "FloatMenuMakerMapInit": "Initializing float menu maker map",
    "GlobalTextureAtlasManagerBakeStaticAtlases": "Texture atlas baking",
    "AbstractFilesystemClearAllCache": "Clearing filesystem cache",
    "LoadModXml": "Loading and parsing mod XML",
}


_PREFIX = "LoadingProgress.StartupImpact."


def metric_label(key: str) -> str:
    if key.startswith(_PREFIX):
        key = key[len(_PREFIX):]
    if key in METRIC_LABELS:
        return METRIC_LABELS[key]
    tail = key.split('.')[-1]
    if tail in METRIC_LABELS:
        return METRIC_LABELS[tail]
    return key
