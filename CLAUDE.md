# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Full agent guide lives in [`AGENTS.md`](./AGENTS.md) — read it too, it has more detail than repeated here.

## Commands

- `just format` — clang-format over `src/` and `tests/` (fastest edit cycle)
- `just check` — `format-check` → `build` → `build-js`
- `just test` — build, then `QT_QPA_PLATFORM=offscreen ctest --preset default`
- `just ci` — `check` then `test`
- `just configure` / `just build` / `just run` — configure the `default` preset, build it, run `build/default/src/app/pxmodrim` with `PX_LOG_LEVEL=debug`
- `just build-release` — `release` preset
- Single test case: `build/default/tests/pxui_tests "case name"`
- `just build-js` — bundles the Steam Workshop TS injection (`src/ui/plugins/steam_workshop/ts/`) to `inject.js` via esbuild; gitignored; mtime-cached

## Architecture

- Entry chain: `src/app/main.cpp` (`runApp()`, DI composition root) → `src/app/plugins.cpp` (the only list of concrete plugins)
- `src/core/` (`pxcore`) — all domain logic, no GUI dependency, never includes `ui/`
- `src/ui/` (`pxui` + QML module `PxModRim`) — Qt Quick views, widgets, plugins; uses `core/` freely
- Plugins live under `src/ui/plugins/` and `src/core/downloads|workshop/`; they must not depend on the shell, and the shell must not name them
- QML files sit directly in `src/ui/` (flat module root); icons come from `image://icons/<name>?color=<hex>`, with QML calling `encodeURIComponent(color)`
- `companion-mods/` — separate C# RimWorld mods (own `AGENTS.md`); `cf-workers/` — Cloudflare Workers deploy; `legacy/` — read-only Python reference

## Conventions

- No comments unless explaining *why*; brief *what* comments allowed only for long blocks.
- Coroutines: `QCoro::Task<T>` with `co_await`; never `QApplication::processEvents()`, `QDialog::exec()`, `QThread`, `sleep`, or `QTimer::singleShot(0, ...)` — use `co_await awaitDialog<T>(...)`, `co_await QCoro::sleepFor(...)`, or run blocking work on a pool and `co_await` it.
- Avoid structured bindings and `co_await` inside ternaries in coroutines (GCC ICE).
- Never global singletons — constructor DI everywhere.
- Never orphan Qt objects — always pass a parent.
- Git renames: `git mv`.
- Ask the user before picking a Qt/QML widget type.
- Always make commits on the go.

## Known stale areas

- The Python-era phases in `ROADMAP.md` describe the old implementation; their commands are historical.
