# PxModRim — Agent guide

## Project state
- **Fork** of RimSort, clean rewrite. Original code preserved untouched in `rimsort-original/`.
- **src-layout**: package lives in `src/pxmodrim/`. UV auto-discovers via `[tool.uv] package = true`.
- **Incremental rewrite**: algorithms extracted from `rimsort-original/`, adapted into new structure.

## Toolchain
- **uv** — `uv sync --dev` (CI uses `uv sync --locked --dev`)
- **just** — task runner, run `just` to list recipes
- **ruff** lint+format — `just ruff-fix` (`ruff check --fix && ruff format`)
- **mypy** + **pyright** — `just typecheck` / `just pyright` / `just check` (runs both)
- Verify with `just check`
- **pytest** — `just test` or `just test-verbose`
- Python **3.12 only** (`requires-python = "==3.12.*"`)
- PySide6 + qasync for async Qt event loop

## Entrypoints
- `just run` (sets `LOGURU_LEVEL=DEBUG`)
- `uv run python -m pxmodrim` (manual)

## Architecture (`src/pxmodrim/`)

```
__main__.py       — entry point: App().run()
_app.py           — bootstrap: QEventLoop, DI assembly (CoreContext + services + MainWindow)
core/
├── context.py    — CoreContext: single source of truth (all_mods, active_uuids, config)
├── mod_service.py— ModService: orchestrates discovery, save, provider management
├── structures.py — CollectionStats (msgspec.Struct)
├── loading.py    — LoadingState: stack-based progress signals for async ops
└── providers/    — BaseModProvider ABC + implementations:
    ├── core.py   — CoreModProvider (game/Mods dir)
    └── local.py  — LocalModProvider + SteamCmdModProvider (both scan local_path, split on PublishedFileId.txt)
services/
├── mod_discovery.py      — scan_mod_directory(), resolve_active_uuids()
├── diagnostics_service.py— DiagnosticsService: checker orchestration, sidebar entries, signals
└── sort_service.py       — SortService: topological sort via asyncio.to_thread
models/
├── metadata/
│   ├── structures.py     — ListedMod, AboutXmlMod, ModsConfig (msgspec structs)
│   └── parsing.py        — About.xml → objects
└── view/
    ├── diagnostics.py    — ModDiagnosticsView, ModIssueView, ModItemState
    └── sidebar.py        — SidebarEntry hierarchy + PROVIDER_LABELS
checker/                  — ModChecker, issue checkers, ConstraintGraph, topological sort
sort/                     — Community rules, tier ordering, sort models
ui/
├── main_window.py        — MainWindow: QMainWindow, manages panels, wires services
├── mod_list_panel.py     — QListView + custom delegate
├── mod_list_model.py     — QAbstractListModel with custom roles (CheckStateRole, ProviderColorRole, etc.)
├── mod_info_panel.py     — Mod details pane
├── sidebar_panel.py      — QML-based sidebar with SidebarEntry model
├── settings_panel.py     — Config dialog with path browsers
├── menu_bar.py           — App menu
├── palette.py + theme.py — Styling / QML theme singleton + style.qss
├── constants.py          — UI constants
└── components/           — Reusable widgets (AccordionSection, BannerWidget, DescriptionRenderer, etc.)
```

## Service wiring (`_app.py`)
- `CoreContext` holds all mod state + config reference
- `ModService(Context, providers[])` handles I/O: discovery + save
- `DiagnosticsService(Context)` runs ModChecker, emits diagnostics/sidebar signals
- `SortService(Context, DiagnosticsService)` runs topological sort in thread
- All passed to `MainWindow(...)` via constructor DI — no singletons

## Rules
- Signal-connected async methods **must** have `@asyncSlot()` from qasync
- Long blocking work → `await asyncio.to_thread(target)`
- **Never** `QApplication.processEvents()` — blocks event loop
- **Never** `dialog.exec()` / `dialog.exec_()` — use `await await_dialog(...)` or `show()`
- **Never** `QTimer.singleShot(0, ...)` to defer — use `await asyncio.sleep(0)`
- **Never** `time.sleep()` in async code — use `await asyncio.sleep()`
- **Never** `QThread` directly — use `asyncio.to_thread()` or `loop.run_in_executor()`
- **Never** blocking I/O in signal handlers or UI methods — move to service layer
- **Never** global singletons — use DI via constructor
- **Never** mix UI and I/O in the same class — UI reads state, services write state
- **Never** `ModType` enum — use `provider_id: str` on `ListedMod`
- `active_uuids` is `list[str]` throughout (not `set[str]`)
- **Strict UI/Logic Separation:** Views and Delegates must remain 100% "dumb" — render pixels and capture raw events only. No business logic, mutations, or I/O inside UI components.

## Donor code (`rimsort-original/`)
- **NEVER modify.** Read-only reference.
- Implement features by understanding the algorithm, then write clean code in `src/pxmodrim/`. Do not copy it id
- Key donor modules: `app/models/metadata/`, `app/sort/`, `app/utils/steam/`, `app/utils/git_utils.py`, `app/utils/github/`

## Key conventions
- All files: `from __future__ import annotations` (PEP 604 style everywhere)
- No comments unless explaining *why* (not *what*)
- `pyproject.toml` has `lint.extend-select = ["I"]` — run `ruff check --fix` after adding imports
- Ruff: line-length 88, double quotes, indent 4
- `plans/ongoing_001.md` contains the architectural plan — consult for design intent

## Testing
- Config: `--import-mode=importlib`, `--no-qt-log`, `pythonpath = 'src'`
- Test tree mirrors `src/pxmodrim/`: `test_metadata/`, `test_compat/`, `test_checker/`, `test_sort/`, `test_ui/`
- UI tests need `QT_QPA_PLATFORM=offscreen` on Linux
- Single test: `uv run pytest tests/test_metadata/test_structures.py -v`
- Mock providers by subclassing `BaseModProvider` for integration tests
