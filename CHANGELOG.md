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
