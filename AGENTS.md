# PxModRim — Agent guide

## Project state
- **Fork** of RimSort with clean architecture. Original code preserved untouched in `rimsort-original/` as reference donor.
- **src-layout**: package lives in `src/pxmodrim/`. UV auto-discovers it via `[tool.uv] package = true`.
- **Incremental rewrite**: algorithms extracted from `rimsort-original/`, adapted and rewritten in new structure.

## Toolchain
- **uv** (not pip) — `uv sync --dev`, never `pip install`
- **just** — task runner, run `just` to list recipes
- **ruff** lint+format — `just ruff-fix` (runs `ruff check --fix && ruff format`)
- **mypy** + **pyright** — `just typecheck` / `just pyright`
- **pytest** — `just test` or `just test-verbose`
- Python **3.12 only**

## Entrypoints
- `just run` — GUI (PySide6 + qasync)
- No CLI mode yet

## Architecture (`src/pxmodrim/`)
- `models/metadata/` — data models (`structures.py`, compiled msgspec dataclasses) and `parsing.py` (About.xml → objects)
- `_compat/` — ported utilities from RimSort donor (`xml.py`, `constants.py`)
- `services/` — async orchestrators (`mod_discovery.py`)
- `ui/` — PySide6 widgets (MainWindow, ModListPanel, ModInfoPanel)
- `_app.py` — bootstrap, QEventLoop wiring
- `__main__.py` — entry point

## Async rules
- Event loop: `qasync.QEventLoop` set in `_app.py`
- Signal-connected async methods **must** have `@asyncSlot()` from qasync, or the coroutine is silently dropped
- Long blocking work → `await asyncio.to_thread(target)`
- No fire-and-forget helpers yet (add when needed)

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
- `ModType` enum setter raises on override (prevents accidental re-classification)
- `pyproject.toml` has `lint.extend-select = ["I"]` for import sorting — run `ruff check --fix` after adding imports

## Testing quirks
- `--import-mode=importlib` (set in pyproject.toml)
- `--no-qt-log` avoids QPA warnings in CI
- `pythonpath = 'src'` for test imports
