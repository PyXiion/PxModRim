# Changelog

All notable changes to PxModRim will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]
### Added
- Settings > Steam Workshop: `Parallel mods` (1-8) and `Threads per mod` (1-16) control pxsteamdl download concurrency, with tooltips. Defaults are 8 and 1.

### Changed
- No Version Warning and Use This Instead databases load from cache instantly and refresh in the background (progress shown in the header) when missing or older than 7 days, instead of blocking startup.
- Organizer is the first view; its left navigation and right mod-info sidebar match Mods in width. Its search, folder actions, selection controls, and tree layout follow the #37 mockup while retaining the existing mod-info sidebar.
- Auto-folder rules show first-match and newly assignable mod counts without blocking the editor; folder expansion controls persist their state.
- Organizer auto-rules open in a centered, resizable window parented to the main window, with the rule list scrolling inside the window.
- Standard auto-folder rules come from popular Workshop collections: exact About.xml package ids for libraries, performance and quality-of-life mods, family prefixes for Vanilla Expanded (including Races/Quests/Storytellers), Alpha, Combat Extended and Dubs. Loose `framework`/`performance` name matching is removed to avoid false positives.
- Enabling mods places them next to load-order constraints and briefly highlights the new row.
- Active mod-list changes show an unsaved indicator and prompt to save, discard, or cancel before closing.
- Startup impact window redesigned: a last-launch summary bar (base game, mods, off-thread), a filterable ranked list of every mod with per-phase bars and expandable breakdowns, and a Phases view grouping game metrics into fixed categories (textures & audio, constructors, patches, defs & XML, deferred init) with per-phase mod lists and optional base-game contribution. Durations of a minute or more show as `4m 29s`.

## [0.1.0] - 2026-09-28
### Added
- Mod organizer (#37): SQLite-backed folder hierarchy, tags, placement rules, and tree resolver; QML TreeView with nested folders, search, source/status/tag filters, bulk toggles, folder actions, organizational drag-and-drop that leaves load order unchanged, the shared mod-info sidebar, and QML editors for tags and ordered auto-folder rules built on shared themed QML controls (`ui/components/controls`); an on-demand set of standard rules (Official, Frameworks & Libraries, Performance, Quality of Life, Vanilla Expanded, Alpha Mods, Combat Extended, Dubs Mods)
- Extended keyboard shortcuts (#35): search, mod toggling, fullscreen, view navigation, full metadata rescan, and a Help menu shortcut reference
- Mod list snapshots and rollback (#24): automatic timestamped backups of `ModsConfig.xml` before save and on launch, configurable retention, and a restore snapshot dialog in the File menu
- Persistent mod metadata cache (#29): SQLite-backed cache keyed by mod path and mtime to avoid re-parsing `About.xml` across launches, with automatic invalidation and corruption recovery
- Native packaging artifacts and CI release pipeline (#36): Linux AppImage/tar, architecture-aware
  .deb/.rpm, and Flatpak with common Steam/RimWorld access; Windows NSIS installer and portable zip;
  ad-hoc-signed macOS app zip and DMG, with optional Developer ID signing and notarization

- Versioning and migration system for config files and database schemas
- `core/migrator.py` — lightweight ordered step migrator
- `core/services.py` — `Service` protocol for `setup()` lifecycle
- Atomic config file saves (write to tmp, os.replace)
- Backup before migration (`config.json.bak.{timestamp}`)
- Managed `config.json` and `ui_prefs.json` use a top-level `schema_version` marker (currently `1`); unversioned files are migrated with a timestamped backup.

### Changed
- Workshop downloads use the [PxSteamDL](https://github.com/PyXiion/PxSteamDL) library instead of SteamCMD: anonymous login in about a second, parallel incremental downloads straight into the local mods folder with SHA-1 verification, and per-mod byte progress in the download queue. The SteamCMD installer, its settings section, the workshop symlink and the "validate downloads" preference are removed; old `steamcmd_prefix`/`validate_downloads` config keys are ignored
- Updated the PxSteamDL source dependency to its cross-platform Linux, macOS, and Windows implementation.

### Fixed
- macOS packaging preserves the `entrypoint` binary alongside its `PxModRim` runtime directory and records the correct `CFBundleExecutable`
- All six official Core/DLC mods (Core, Royalty, Ideology, Biotech, Anomaly, Odyssey) now show their names instead of "Unknown Mod Name"; stale metadata-cache entries are reparsed after upgrade
- Workshop downloads accept only numeric Workshop IDs; queued/downloading rows can no longer be removed mid-batch
- Mod description links restricted to HTTP(S); oversized `<size>`/`<indent>` values no longer crash rendering
- Saving `ModsConfig.xml` preserves unknown elements and attributes
- Startup no longer crashes on config failure cleanup, and closing the window during startup shuts down cleanly
- Closing the last window waits for asynchronous plugin shutdown before stopping the Qt event loop
- Plugins shut down in reverse dependency order
- Dependency cycle detection reports all overlapping cycles (SCC-based); sorting/cycle checks no longer hit recursion limits on long chains; tier sort no longer quadratic
- `About.xml` version keys match exactly (`v1.5` no longer matches `v1.50`); mods without `packageId` are invalid
- Stale time-analytics results no longer overwrite the selected mod; sidebar icons update with entries; proxy model keeps persistent indexes valid across filtering
- Startup-impact database writes are serialized

### Removed
- Unused per-mod `community_rules`/`user_rules` and `overall_rules` merge; unused `DownloadRunner` protocol
