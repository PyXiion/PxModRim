# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Full agent guide lives in [`AGENTS.md`](./AGENTS.md) — read it too, it has more detail than repeated here.

## Commands

- `just fix` — ruff lint fix + format (fastest edit cycle)
- `just check` — ruff fix -> `build-js` -> pyright -> `check-deps` (matches CI's check step)
- `just test` — `build-js` + `dev-setup` -> `uv run pytest --doctest-modules --no-qt-log -s`
- `just ci` — `check` then `test` (full CI pipeline)
- `just run` — `build-js` + `dev-setup` -> `LOGURU_LEVEL=DEBUG uv run python -m pxmodrim`
- Single test: `uv run pytest tests/test_metadata/test_structures.py -v`
- `just bench <name>` — run `tests/benchmarks/bench_*.py` (name filters by substring, or omit for all)
- `just build-js` — bundles Steam Workshop TS injection (`ui/plugins/steam_workshop/ts/`) to `inject.js` via esbuild; gitignored, must build before running; mtime-cached

## Architecture

- Entry chain: `__main__.py` -> `_app.py` (`App().run()`, DI composition root, Fusion style + QPalette)
- `core/` — all domain logic, never imports `ui/`
- `ui/` — Qt widgets + QML, imports `core/` freely
- Layer boundaries are enforced by `scripts/check-deps.py` (`just check-deps`), which defines 23 groups with explicit allowed-dependency matrices via pydeps. A new cross-layer import that violates the rules must update that file's `ALLOWED` dict, not bypass the checker.
- Plugin system: plugins (e.g. `ui/plugins/steam_workshop/`) must not know about UI layers; layers must not know about plugins.
- QML files sit next to their Python panel (e.g. `ui/components/Header.qml` next to `header_panel.py`).
- Icons served via `image://icons/<name>?color=<hex>` through `SvgIconProvider` on the shared `QQmlEngine`. QML must `encodeURIComponent(color)`; the provider does `urllib.parse.unquote`.
- Test tree mirrors `src/pxmodrim/`. Mock providers by subclassing `BaseModProvider`.
- `companion-mods/` — separate C# RimWorld mods (own `AGENTS.md`); `cf-workers/` — Cloudflare Workers deploy; `rimsort-original/` — vendored reference, excluded from lint/type-check.

## Conventions

- `from __future__ import annotations` in every `.py` file.
- No comments unless explaining *why*; brief *what* comments allowed only for long blocks.
- Async signal handlers need `@asyncSlot()` from `qasync`.
- Never `QApplication.processEvents()`, `dialog.exec()`, `QThread`, `time.sleep()`, `QTimer.singleShot(0, ...)` — use `await asyncio.to_thread()`, `await await_dialog()`, `await asyncio.sleep(0)`.
- Never global singletons — constructor DI everywhere.
- Long blocking work -> `await asyncio.to_thread(target)`.
- Git renames: `git mv`, never `shutil.move`.
- Never orphan Qt objects — always pass a parent.
- Ask the user before picking a Qt/QML widget type.

## Known stale areas

- `core/loading.py`'s `LoadingState` QObject is planned to move to `ui/progress.py`; three files still import from `core/loading`.
- `core/models/view/` holds view models that may move to `ui/`.
- Config UI has an `if` branch for the SteamCMD plugin, planned to become a real plugin.
