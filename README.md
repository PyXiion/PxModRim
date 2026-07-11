# PxModRim

An asynchronous, lightweight mod manager for RimWorld — Python + PySide6, Data-Driven Architecture.

---

## Why PxModRim?

- **Async first** — Disk scanning never freezes the UI. The interface stays responsive no matter how many mods you have.
- **Provider model** — Steam, Local, and Core mods are separated by design. No path-based guessing games to determine mod type.
- **Clean polymorphic sidebar** — All / Active / Inactive / By source / With errors. Filtering logic lives in strategy objects, not in `if/else` chains.
- **Data-driven state** — Single source of truth (`CoreContext`). UI only reads immutable slices; it never writes to disk directly.
- **Modern stack** — Python 3.12, PySide6, qasync, uv, ruff, mypy, pyright, pytest.

---

## Quick Start

```bash
uv sync --dev
just run
```

All commands: `just`

---

## Why Not RimSort?

RimSort is the only usable mod manager for RimWorld — respect to the author. But the codebase has structural problems that patches can't fix:

- **God objects** — `MainContent` (118 imports) mixes UI, game launching, file scanning, import/export, and ZIP installs. A 1041-line `eventFilter()` handles menus, `os.rename()`, SteamDB lookups, and dialogs all in one method.
- **Magic instead of types** — Mod type is guessed by comparing folder paths against hardcoded directories. Move a folder — its "type" changes. `PublishedFileId` falls back to "is the folder name numeric?".
- **Zero async** — All I/O runs on the UI thread. `QApplication.processEvents()` appears 11 times as a band-aid. `time.sleep()` in signal handlers. Three different concurrency models (asyncio, QThread, ThreadPoolExecutor) with no abstraction.
- **Global singletons everywhere** — 9+ singletons, including the main view. Testing in isolation is practically impossible.

This isn't fixable incrementally — it needed a rewrite from scratch with clean boundaries.

---

## Architecture (Brief)

```
UI Layer (Qt)          I/O Boundary           Core State
┌─────────────────┐    ┌──────────────────┐   ┌─────────────────┐
│ MainWindow      │───▶│ ModService       │───▶│ CoreContext     │
│ ModListPanel    │    │ (orchestrator)   │    │ (dict, set,     │
│ SidebarPanel    │    │                  │    │  AppConfig)     │
│ ModInfoPanel    │    │ CoreProvider     │    │                 │
└─────────────────┘    │ LocalProvider    │    │ Immutable       │
                       │ SteamCmdProvider │    │ slices only     │
                       └──────────────────┘    └─────────────────┘
```

- **CoreContext** — Pure in-memory state (`dict[str, ListedMod]`, `set[str]` active UUIDs). No I/O. Properties return copies.
- **BaseModProvider** — Each provider owns its disk sector and sets `provider_id` on mods. No shared path-comparison logic.
- **ModService** — Coordinates providers, merges results, resolves active UUIDs, updates context.
- **SidebarEntry (Strategy)** — `AllModsEntry`, `ActiveModsEntry`, `InactiveModsEntry`, `ProviderModsEntry`, `ErrorModsEntry`. Filtering is polymorphic; `MainWindow` just calls `show_uuids(entry.visible_uuids)`.

---

## Performance Notes

- `qasync.QEventLoop` — Real async event loop. `ModService.discover()` runs in an executor.
- `@asyncSlot()` on all signal-connected coroutines — No silently dropped coroutines.
- `show_uuids(set[str])` — Python-level set membership check (`O(1)`), Qt `setHidden()` only for changed rows.

---

## Roadmap

- [ ] Sorting (algorithms ported from RimSort)
- [ ] Drag & Drop reordering in ModListPanel
- [ ] SteamCMD / Steam Web API integration
- [ ] `_item_cache` for `O(1)` visibility toggling
- [ ] Plugin/subscription system

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

## Technical Deep-Dive: Why Not RimSort?

> The abbreviated version above covers the user-facing reasons. Below is the forensic evidence.

### God Object: `MainContent` (`main_content_panel.py`)

- 118 imports spanning virtually every subsystem
- Singleton (`_instance`, `__new__` override)
- Responsibilities: UI layout, game launch (`QProcess`), mod scanning, import/export (Rentry, XML), ZIP installs, database building, Steam Workshop DB updates

### 1041-Line `eventFilter` (`mods_panel.py:1488`)

Single method handling **all** right-click actions:
- Qt menu construction
- Filesystem I/O (`os.rename`, `os.path.exists`)
- SteamDB / blacklist lookups
- PublishedFileId resolution
- ACF metadata queries
- `QInputDialog`, `QMessageBox`
- Mod deletion / re-download logic

### Mod Type by Path Comparison (`metadata_factory.py:376-420`)

```python
parent_path = mod.mod_path.parent
if parent_path == workshop_path:
    mod.mod_type = ModType.STEAM_WORKSHOP
elif parent_path == local_path:
    if (mod.mod_path / "About/PublishedFileId.txt").exists():
        mod.mod_type = ModType.STEAM_CMD
    else:
        mod.mod_type = ModType.LOCAL
```

Type is not a property of the mod — it's a property of **which folder the mod happens to be in**.

### Sync I/O on UI Thread

| Pattern | Count | Locations |
|---------|-------|-----------|
| `QApplication.processEvents()` | 11 | `player_log_tab.py`, `main_content_panel.py`, `animations.py` |
| `time.sleep()` | 5 | `git_utils.py`, `update_utils.py` |
| `QProgressDialog` + ad-hoc `QThread` | Multiple | `mods_panel.py`, workers scattered |
| XML parsing in `@Slot()` | 2+ | `main_content_panel.py`, `metadata_controller.py` |

Three concurrency models (asyncio, QThread, ThreadPoolExecutor) with no unifying abstraction.

### 9+ Global Singletons

`MainContent`, `MetadataController`, `EventBus`, `SteamcmdInterface`, `AppInfo`, `DatabaseBuilder`, `GUIInfo`, `SystemInfo`, `LoadingOverlayManager` — all module-level `_instance = None`. The **view** is a singleton.

### Function Length Distribution

- 1 function: **1041 lines** (`eventFilter`)
- 27 functions: **> 100 lines**
- Deep `if/elif` chains for type dispatch throughout

---

## License

MIT — see `LICENSE` file.