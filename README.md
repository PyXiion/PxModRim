# PxModRim

An asynchronous, lightweight mod manager for RimWorld &mdash; Python + PySide6, Data-Driven Architecture.

---

## Why PxModRim?

- **Async first** &mdash; Disk scanning never freezes the UI. The interface stays responsive no matter how many mods you have.
- **Provider model** &mdash; Steam, Local, and Core mods are separated by design. No path-based guessing games to determine mod type.
- **Clean polymorphic sidebar** &mdash; All / Active / Inactive / By source / With errors. Filtering logic lives in strategy objects, not in `if/else` chains.
- **Data-driven state** &mdash; Single source of truth (`CoreContext`). UI only reads immutable slices; it never writes to disk directly.
- **Modern stack** &mdash; Python 3.12, PySide6, qasync, uv, ruff, mypy, pyright, pytest.

---

## Why Not RimSort?

RimSort is the only usable mod manager for RimWorld &mdash; respect to the author. But the codebase has structural problems that patches can't fix:

- **God objects** &mdash; `MainContent` (118 imports) mixes UI, game launching, file scanning, import/export, and ZIP installs. A 1041-line `eventFilter()` handles menus, `os.rename()`, SteamDB lookups, and dialogs all in one method.
- **Magic instead of types** &mdash; Mod type is guessed by comparing folder paths against hardcoded directories. Move a folder &mdash; its "type" changes. `PublishedFileId` falls back to "is the folder name numeric?".
- **Zero async** &mdash; All I/O runs on the UI thread. `QApplication.processEvents()` appears 11 times as a band-aid. `time.sleep()` in signal handlers. Three different concurrency models (asyncio, QThread, ThreadPoolExecutor) with no abstraction.
- **Global singletons everywhere** &mdash; 9+ singletons, including the main view. Testing in isolation is practically impossible.

This isn't fixable incrementally &mdash; it needed a rewrite from scratch with clean boundaries.

---

## Development

```bash
# Install deps
uv sync --dev

# Run GUI
just run

# Tests
just test

# Lint + format
just ruff-fix

# Typecheck
just typecheck
# or
just pyright
```

---

## License

MIT