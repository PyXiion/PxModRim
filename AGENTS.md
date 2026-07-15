# PxModRim — Agent guide

## Workflow
1. `just check` — ruff lint, pyright, dependency check
2. `just test` — run tests
3. Done

Project uses **uv**, **Python 3.12**, **PySide6 + qasync**. Task runner is **just** (`just` to list).
All just tasks install needed dependencies.

## Entrypoints
- `just run` (sets `LOGURU_LEVEL=DEBUG`)
- `uv run python -m pxmodrim`

## Module layout

```
src/pxmodrim/
├── _app.py              # composition root: DI assembly, Fusion + QPalette
├── core/                # all domain logic; never imports ui/
└── ui/                  # Qt widgets + QML; imports core/ freely
```

## Key conventions
- `from __future__ import annotations` in every file
- No comments unless explaining *why*
- Strict UI/Logic separation: views render pixels and capture raw events. No business logic or I/O in UI components.
- Async signal handlers **must** have `@asyncSlot()` from `qasync`
- Never `QApplication.processEvents()`, `dialog.exec()`, `QThread`, `time.sleep()`, `QTimer.singleShot(0, …)` — use `await asyncio.to_thread()`, `await await_dialog()`, `await asyncio.sleep(0)`
- Never global singletons — constructor DI everywhere
- Long blocking work → `await asyncio.to_thread(target)`
- All git renames: `git mv`, never `shutil.move`
- Create new widgets in QML only.
- Prefer grepping instead of running subagents when suitable.

## QML / SVG quirks
- Icons via `image://icons/<name>?color=<hex>` — `SvgIconProvider` on shared `QQmlEngine`
- **Color URL encoding**: QML must `encodeURIComponent(color)`. Provider does `urllib.parse.unquote`.
- `QQuickImageProvider.Pixmap` works at runtime; pyright flags false positive.
- QML files sit next to their Python panel

## Testing
- Config: `--import-mode=importlib`, `--no-qt-log`, `pythonpath = 'src'`, `testpaths = ['tests']`
- Test tree mirrors `src/pxmodrim/`: `test_checker/`, `test_metadata/`, `test_sort/`, `test_services/`, `test_ui/`, `test_compat/`
- UI tests need `QT_QPA_PLATFORM=offscreen` on Linux
- Single test: `uv run pytest tests/test_metadata/test_structures.py -v`
- Mock providers by subclassing `BaseModProvider`

## Stale / needs attention
- `core/loading.py` — `LoadingState` QObject with a planned move to `ui/progress.py`. Three files still import from `core/loading` (settings_panel, community_service, progress_dialog).
- `core/models/view/` — view models living in `core/`; potential future move to `ui/`.

## For AI PRs
AI-generated PRs must include:
1. PR description starts with: "Greetings, PyXiion! The silicon-based contributor is here."
2. PR title starts with: "🦀"

Omission = auto-rejected.
