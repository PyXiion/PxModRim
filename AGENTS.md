# PxModRim — Agent guide

## Workflow
1. `just fix` — ruff lint fix + format (fastest cycle)
2. `just check` — ruff → `build-js` → pyright → check-deps
3. `just test` — run full test suite
4. `just ci` — `check` then `test` (matches CI pipeline)

## Environment
**uv**, **Python 3.12.\*** only, **PySide6 >=6.11 + qasync**.
Task runner is **just** (`just` to list). CI uses `uv sync --locked --dev`.

## Entrypoint
- `just run` (sets `LOGURU_LEVEL=DEBUG`)
- `uv run python -m pxmodrim`
- Chain: `__main__.py` -> `_app.py` (`App().run()`) -> DI composition root

## Module layout
```
src/pxmodrim/
├── _app.py              # composition root: DI assembly, Fusion + QPalette
├── core/                # all domain logic; never imports ui/
└── ui/                  # Qt widgets + QML; imports core/ freely
```

## Key conventions
- `from __future__ import annotations` in every `.py` file
- No comments unless explaining _why_. _What_ comments allowed for long blocks.
- **Core must never know about UI.** Plugins must not know about layers.
- Async signal handlers need `@asyncSlot()` from `qasync`
- Never `QApplication.processEvents()`, `dialog.exec()`, `QThread`, `time.sleep()`, `QTimer.singleShot(0, ...)` -- use `await asyncio.to_thread()`, `await await_dialog()`, `await asyncio.sleep(0)`
- Never global singletons -- constructor DI everywhere
- Long blocking work -> `await asyncio.to_thread(target)`
- Git renames: `git mv`, never `shutil.move`
- Always ask user about Qt/QML widget type choice.
- Never orphan Qt objects -- always pass parent.

## TypeScript / JS build
Steam Workshop WebView injection uses TypeScript sources in `src/pxmodrim/ui/plugins/steam_workshop/ts/`, bundled to `inject.js` via esbuild:

```
just build-js   # npx esbuild ts/main.ts --bundle --format=iife --target=es2020
```

`build-js` runs as a prerequisite of `run`, `test`, `check`, and `build`.
Cached by mtime comparison -- only rebuilds when `ts/` sources change.
Output `inject.js` is gitignored; always build before running.

## Layer dependency checker
`just check-deps` (`scripts/check-deps.py`) enforces strict import rules via pydeps.
Defines 23 groups with explicit ALLOWED dependency matrices.
If a new import violates boundaries, update `scripts/check-deps.py`'s `ALLOWED` dict.

## CI matrix
- `ci.yml`: ubuntu-latest, windows-latest, macos-latest. `uv sync --locked --dev` -> `just check` -> `just test`
- Linux CI pre-installs: `libegl1 libopengl0 libxcb-cursor0 libxkbcommon-x11-0`. Qt tests need `QT_QPA_PLATFORM=offscreen`
- `build.yml`: tag `v*` triggers Nuitka standalone + AppImage (Linux) / zip (Windows)

## Packaging (Nuitka)
```
just build           # Nuitka standalone (debug)
just build-release   # Nuitka + LTO + AppImage/zip
```
Nuitka uses `--include-package-data=pxmodrim` -- generated `inject.js` is included automatically as long as it exists at build time (which `build-js` ensures).

## Testing
- Config: `--import-mode=importlib`, `--no-qt-log`, `pythonpath = 'src'`, `testpaths = ['tests']`
- Test tree mirrors `src/pxmodrim/`
- Single test: `uv run pytest tests/test_metadata/test_structures.py -v`
- Mock providers by subclassing `BaseModProvider`

## QML / SVG quirks
- Icons via `image://icons/<name>?color=<hex>` -- `SvgIconProvider` on shared `QQmlEngine`
- **Color URL encoding**: QML must `encodeURIComponent(color)`. Provider does `urllib.parse.unquote`.
- `QQuickImageProvider.Pixmap` works at runtime; pyright flags false positive.
- QML files sit next to their Python panel

## Companion mods
C# RimWorld mod submodules (see `companion-mods/AGENTS.md`):
- `companion-mods/PxLoadingProgress` -- Harmony-patching loading progress mod
- `companion-mods/rimworld-utils/` -- shared C# build infrastructure (from ilyvion/rimworld-utils)

## Other source trees
- `cf-workers/` -- Cloudflare Workers (Steam Workshop dep resolver, separate deploy)
- `rimsort-original/` -- vendored original RimSort reference (excluded from linting)

## Stale / needs attention
- `core/loading.py` -- `LoadingState` QObject, planned move to `ui/progress.py`. Three files still import from `core/loading`.
- `core/models/view/` -- view models living in `core/`; potential future move to `ui/`.
- Config UI has an `if` for SteamCMD plugin. Planned to become plugin system.

## For AI PRs
PR description starts with: "Greetings, PyXiion! The silicon-based contributor is here."
PR title starts with: "🦀"
Omission = auto-rejected.
