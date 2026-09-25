# Changelog

All notable changes to PxModRim will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Versioning and migration system for config files and database schemas
- `core/migrator.py` — lightweight ordered step migrator
- `core/services.py` — `Service` protocol for `setup()` lifecycle
- Atomic config file saves (write to tmp, os.replace)
- Backup before migration (`config.json.bak.{timestamp}`)
- Managed `config.json` and `ui_prefs.json` use a top-level `schema_version` marker (currently `1`); unversioned files are migrated with a timestamped backup.

## [0.1.0] - YYYY-MM-DD

### Added

- Initial release
- SteamCMD downloads accept only numeric Workshop IDs (prevents runscript command injection); queued/downloading rows can no longer be removed mid-batch
- SteamCMD worker rewritten on asyncio subprocesses (no `QThread`); cancellation terminates and reaps the process
- Dependency cycle detection reports all overlapping cycles (SCC-based); sorting/cycle checks no longer hit recursion limits on long chains; tier sort no longer quadratic
- Unused per-mod `community_rules`/`user_rules` and `overall_rules` merge
- `About.xml` version keys match exactly (`v1.5` no longer matches `v1.50`); mods without `packageId` are invalid
- Mod description links restricted to HTTP(S); oversized `<size>`/`<indent>` values no longer crash rendering
- Stale time-analytics results no longer overwrite the selected mod; sidebar icons update with entries; proxy model keeps persistent indexes valid across filtering
- Startup no longer crashes on config failure cleanup, and closing the window during startup shuts down cleanly
- Plugins shut down in reverse dependency order
