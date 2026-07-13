# PxModRim — Agent guide

## Project state
- **Fork** of RimSort, clean rewrite. Original code in `rimsort-original/` — read-only.
- **src-layout**: package in `src/pxmodrim/`. UV auto-discovers via `[tool.uv] package = true`.
- **Incremental rewrite**: algorithms extracted from `rimsort-original/`, adapted into new structure.

## Toolchain
- **uv** — `uv sync --dev` (CI uses `uv sync --locked --dev`)
- **just** — task runner, `just` to list
- **ruff** lint+format: `just ruff-fix` / `just fix` (both check+format)
- **mypy** (on `src/`) + **pyright** (on `src/ tests/`): `just typecheck` / `just pyright` / `just check` (runs both sequentially)
- **pytest**: `just test` or `just test-verbose`
- **`just ci`** = `check` then `test`
- Python **3.12 only** (`requires-python = "==3.12.*"`)
- PySide6 + qasync for async Qt event loop

## Entrypoints
- `just run` (sets `LOGURU_LEVEL=DEBUG`)
- `uv run python -m pxmodrim`

## Architecture (`src/pxmodrim/`)
- `__main__.py` → `_app.py` (App class, DI assembly, Fusion style + dark QPalette)
- `_app.py` wires: `CoreContext` → `ModService(CoreContext, providers[])` + `DiagnosticsService(CoreContext)` + `SortService(Ctx, Diag)` → `MainWindow(...)` via constructor DI — no singletons
- `core/context.py` — CoreContext: single source of truth (all_mods, active_uuids, config)
- `core/providers/` — `BaseModProvider` ABC; `core.py` (game/Mods), `local.py` (local_path), `workshop.py` (workshop_path)
- `services/` — `mod_discovery.py` (scan + resolve), `diagnostics_service.py` (checker orchestration + sidebar signals), `sort_service.py` (topological sort via `asyncio.to_thread`)
- `checker/` — ModChecker, issue checkers, ConstraintGraph
- `sort/` — Community rules, tier ordering
- `models/metadata/` — `ListedMod`, `AboutXmlMod`, `ModsConfig` (msgspec structs); `parsing.py` (About.xml → objects)
- `models/view/` — `ModDiagnosticsView`, `ModIssueView`, `SidebarEntry` hierarchy
- `ui/` — 3-panel layout: sidebar (QML), mod-list (QML), mod-info (QWidget); title bar, header (QML), toast overlay
- `ui/palette.py` + `theme.py` + `style.qss` — color tokens, QML theme singleton, QSS templated via `string.Template`
- `ui/components/` — reusable widgets: AccordionSection, MetaChip/MetaChipRow, AspectRatioBanner, DescriptionRenderer, Toast/ToastManager, TitleBar, IconButton, SvgIconProvider

## Key conventions
- `from __future__ import annotations` in every file (PEP 604)
- No comments unless explaining *why*
- `active_uuids` is `list[str]` throughout (not `set[str]`)
- No `ModType` enum — use `provider_id: str` on `ListedMod`
- Strict UI/Logic separation: views render pixels and capture raw events only. No business logic or I/O in UI components.
- Long blocking work → `await asyncio.to_thread(target)`
- Async signal handlers **must** have `@asyncSlot()` from `qasync`
- Never `QApplication.processEvents()` (blocks loop), `dialog.exec()` (use `await await_dialog(...)` or `show()`), `QThread` (use `asyncio.to_thread`), `time.sleep()` (use `await asyncio.sleep()`), `QTimer.singleShot(0, …)` (use `await asyncio.sleep(0)`)
- Never global singletons — use constructor DI

## QML / SVG quirks
- Icons served via `image://icons/<name>?color=<hex>` protocol — `SvgIconProvider` registered on a shared `QQmlEngine` in MainWindow.
- **Color URL encoding**: QML must use `encodeURIComponent(color)`. The provider `urllib.parse.unquote`s it.
- **Missing xmlns**: `svg_str()` injects `xmlns="http://www.w3.org/2000/svg"`.
- `QQuickImageProvider.Pixmap` works at runtime but pyright flags it as a false positive.

## Testing
- Config: `--import-mode=importlib`, `--no-qt-log`, `pythonpath = 'src'`, `testpaths = ['tests']`
- Test tree mirrors `src/pxmodrim/`: `test_metadata/`, `test_compat/`, `test_checker/`, `test_sort/`, `test_ui/`
- UI tests need `QT_QPA_PLATFORM=offscreen` on Linux
- Single test: `uv run pytest tests/test_metadata/test_structures.py -v`
- Mock providers by subclassing `BaseModProvider` for integration tests

## Design references
- `reference.html` — visual target for UI redesign (Discord/Steam aesthetic)
