# Changelog

All notable changes to PxModRim will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
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

### Fixed
- macOS packaging preserves the `entrypoint` binary alongside its `PxModRim` runtime directory and records the correct `CFBundleExecutable`
- SteamCMD downloads accept only numeric Workshop IDs (prevents runscript command injection); queued/downloading rows can no longer be removed mid-batch
- SteamCMD worker rewritten on asyncio subprocesses (no `QThread`); cancellation terminates and reaps the process
- Mod description links restricted to HTTP(S); oversized `<size>`/`<indent>` values no longer crash rendering
- Saving `ModsConfig.xml` preserves unknown elements and attributes
- Startup no longer crashes on config failure cleanup, and closing the window during startup shuts down cleanly
- Plugins shut down in reverse dependency order
- Dependency cycle detection reports all overlapping cycles (SCC-based); sorting/cycle checks no longer hit recursion limits on long chains; tier sort no longer quadratic
- `About.xml` version keys match exactly (`v1.5` no longer matches `v1.50`); mods without `packageId` are invalid
- Stale time-analytics results no longer overwrite the selected mod; sidebar icons update with entries; proxy model keeps persistent indexes valid across filtering
- Startup-impact database writes are serialized

### Removed
- Unused per-mod `community_rules`/`user_rules` and `overall_rules` merge; unused `DownloadRunner` protocol

## [0.1.0] - YYYY-MM-DD

### Added

- Initial release
