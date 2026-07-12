# PxModRim — Agent guide

## Project state
- **Fork** of RimSort with clean architecture. Original code preserved untouched in `rimsort-original/` as reference donor.
- **src-layout**: package lives in `src/pxmodrim/`. UV auto-discovers it via `[tool.uv] package = true`.
- **Incremental rewrite**: algorithms extracted from `rimsort-original/`, adapted and rewritten in new structure.

## Toolchain
- **uv** — `uv sync --dev`
- **just** — task runner, run `just` to list recipes
- **ruff** lint+format — `just ruff-fix` (runs `ruff check --fix && ruff format`)
- **mypy** + **pyright** — `just typecheck` / `just pyright`
- **pytest** — `just test` or `just test-verbose`
- Python **3.12 only**

## Entrypoints
- `just run`

## Architecture (`src/pxmodrim/`)
- `models/metadata/` — data models (`structures.py`, compiled msgspec dataclasses) and `parsing.py` (About.xml → objects)
- `_compat/` — ported utilities from RimSort donor (`xml.py`, `constants.py`)
- `services/` — async orchestrators (`mod_discovery.py`)
- `ui/` — PySide6 widgets (MainWindow, ModListPanel, ModInfoPanel)
- `_app.py` — bootstrap, QEventLoop wiring
- `__main__.py` — entry point

## Rules
- Signal-connected async methods **must** have `@asyncSlot()` from qasync, or the coroutine is silently dropped
- Long blocking work → `await asyncio.to_thread(target)`
- No fire-and-forget helpers yet (add when needed)
- Do not write smelly code.
- **Never** `QApplication.processEvents()` — blocks event loop, use async instead
- **Never** `dialog.exec()` or `dialog.exec_()` — blocks UI thread, use async dialogs or `show()`
- **Never** `QTimer.singleShot(0, ...)` to defer work — use `await asyncio.sleep(0)` or proper async
- **Never** `time.sleep()` in async code — use `await asyncio.sleep()`
- **Never** `QThread` directly — use `asyncio.to_thread()` or `loop.run_in_executor()`
- **Never** blocking I/O in signal handlers or UI methods — move to service layer
- **Never** global singletons (`_instance = None` pattern) — use DI via constructor
- **Never** mix UI and I/O in same class — UI reads state, services write state
- **Never** `ModType` enum (removed) — use `provider_id: str` on `ListedMod`
- **Strict UI/Logic Separation:** Views and Delegates must remain 100% "dumb"—their only job is 
rendering pixels and capturing raw events. Never include business logic, direct data 
mutations, or any I/O operations (os, json, databases) inside UI components. All data 
interaction must go strictly through index.data(CustomRole) and model.setData().

## Donor code (`rimsort-original/`)
- **NEVER modify** `rimsort-original/`. It's read-only reference.
- When implementing a feature, look at the donor implementation, understand the algorithm, write clean code in `src/pxmodrim/` (don't copy verbatim).
- Key donor modules for reference:
  - `rimsort-original/app/models/metadata/` — structures, factory, mediator
  - `rimsort-original/app/sort/` — sorting algorithms
  - `rimsort-original/app/utils/steam/` — steam integration
  - `rimsort-original/app/utils/git_utils.py` — git operations
  - `rimsort-original/app/utils/github/` — GitHub mod install

## Key conventions
- All files: `from __future__ import annotations` (PEP 604 style everywhere)
- No comments unless explaining *why* (not *what*)
- `pyproject.toml` has `lint.extend-select = ["I"]` for import sorting — run `ruff check --fix` after adding imports

## Testing quirks
- `--import-mode=importlib` (set in pyproject.toml)
- `--no-qt-log` avoids QPA warnings in CI
- `pythonpath = 'src'` for test imports
