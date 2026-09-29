# Backlog

Findings from the review of `origin/main..main` (2026-09-30) that are not fixed yet.
Paths are relative to `src/pxmodrim/` unless noted.

## Medium

### UI
- **Sidebar has no initial selection** (`ui/panels/sidebar_panel.py`, `ui/components/FilterSidebar.qml`). Old `Sidebar.qml` selected "All" via `onCurrentIndexChanged`; `FilterSidebar` emits only on click, so `current_entry()` is `None` at startup. After a model reset `currentIndex` can stay out of range. Clamp to `select_all_entry` and re-emit.
- **`ModListModel.load_mods` `KeyError`** (`ui/models/mod_list_model.py:356`). The `if uuid in mods` guard was dropped; an active uuid missing from `mods` raises between `beginResetModel`/`endResetModel` and leaves a stale list.
- **Launch-strategy menu is clipped** (`ui/components/Header.qml:313`). `PxMenu` opens inside the 80px QQuickWidget and covers the window controls and Play. Use a Python `QMenu` or a taller host.
- **Collapsed `IconRail` tooltip** (`ui/components/IconRail.qml:120`). Wrapped and clipped to 52px. Use a native tooltip or a wider host.
- **`PxMenu` does not size to content** (`ui/components/controls/PxMenu.qml`). Width stays 160; compute `Math.max` of `itemAt(i).implicitWidth`. `interactive` should compare with `control.height`.
- **Downloads panel** (`ui/plugins/downloads/`).
  - Phase (`login`/`query`) is not cleared when a batch ends; call `set_phase("")` in `finish()`.
  - Filter chip `Repeater` model is a JS array rebuilt on every `summary_changed` (~6/s); use a fixed model.
  - Cancelled rows are counted as failed ("Retry failed (N)" after a Stop).
  - `retryFailed` errors are only logged.
- **Update confirmation is inconsistent** (`ui/components/mod_updates.py`, `ui/plugins/organizer/view.py:386,838`, `ui/panels/mod_list_panel.py:293`). `CONFIRM_THRESHOLD` applies only via `MainWindow._update_mods`; other paths update 25+ mods silently and swallow `RuntimeError`/`ValueError` with a log line. Unify into one confirm+report path.
- **"Check for Updates" is silent while a startup check runs** (`ui/window/main_window.py`). Toast "already checking" or await the running task.
- **Mod info description links** (`ui/panels/mod_info_data.py`, `ui/panels/ModInfo.qml:333`).
  - `autolink` always strips a trailing `)`: `Foo_(bar)` becomes `Foo_(bar`. Strip only when unbalanced; `&amp;` at the end splits.
  - Links go straight to `Qt.openUrlExternally`; safe only because of upstream sanitisation. Emit a signal and check the scheme in Python.
  - Any https `[img]` is fetched on selection (IP leak); decide on click-to-load.
  - Link button is shown for non-http(s) URLs but does nothing; compute `canOpenUrl` in `build_mod_info`.

### Settings
- **Save drops fields without UI** (`ui/panels/settings_panel.py:209`). `PathConfig(...)` resets `community_rules_file`, `no_version_warning_file`, `use_this_instead_file`; `SortSettings(...)` resets `tier_config`. Use `msgspec.structs.replace` / `dataclasses.replace`.
- **Auto-update combo clobbers unknown values** (`ui/panels/Settings.qml:214`). 48h becomes 0 on save. Add the current value to the model or keep it when `indexOfValue < 0`.
- **Background actions outlive the dialog** (`settings_panel.py:145-168`). `downloadCommunityRules`/`clearCache` touch deleted QObjects after close. Track and cancel, or guard with `shiboken6.isValid`.

### Upload report
- **HTTP 206 treated as success** (`core/support/report.py:upload`). paste.rs truncates oversize pastes; reject 206 or shrink `max_log_bytes`.
- **Uncaught errors** in `PasteUploader.upload`: `UnicodeDecodeError` and `http.client.HTTPException` escape `handle_upload_report`. Convert to `ReportUploadError`, decode with `errors="replace"`, cap `read()`.
- **About dialog closed during upload** (`ui/panels/about_panel.py:312`, `ui/window/main_window.py:557`). `setEnabled` on a deleted panel raises `RuntimeError`; the failure dialog gets a deleted parent.

### Downloads / Steam
- **Failed mods retried every 10 min** (`core/downloads/steam.py:_auto_update_once`). No last-attempt record or backoff; offline users may get a repeating toast.
- **Auto-update loop dies silently** on any exception other than `RuntimeError`/`ValueError` (`_auto_update_loop`, `_run`).
- **Broken client is cached** (`steam.py:_client`). Reset `self._client = None` when `client.download` raises `RuntimeError`.
- **Manager single-flight is bypassable** (`core/downloads/manager.py`, `ui/plugins/steam_workshop/plugin.py:183,189`). Steam tab and auto-updater call the downloader directly, skipping the manager's busy/cancel state.
- **Steam workshop plugin** (`ui/plugins/steam_workshop/`). Sidebar not synced with `is_downloading` if first shown mid-download; `request_download` leaves items "queued" if `download_mods` raises.

### Core
- **Concurrent `save_active_layout`** (`core/mod_service.py:225`). Now runs in `to_thread`; overlapping saves can race snapshot rotation. Add an `asyncio.Lock`.

### Docs
- **`CHANGELOG.md` incomplete**: no entries for the `DownloadManager` and optional Steam (`steam` extra, `PX_DISABLED_PLUGINS`), Downloads tab, Workshop update actions and auto-update, header progress, GitHub update check (#27), QML settings and mod-info panels, `pxsteamdl` 0.2.0.
- **`AGENTS.md` stale**: `uv sync --locked --dev` vs the `--extra steam` workflows; "Defines 24 groups" (now 26).

## Low

### Dead code and stale config
- `ui/theme/constants.py`: `ACCORDION_ANIMATION_DURATION_MS`, `ACCORDION_EASING` (and the `QEasingCurve` import).
- `ui/theme/style.qss`: rules for `accordion*`, `metaChip*`, `metaLabel`, `metaValue`, `descriptionBrowser`; `text-transform` / `letter-spacing` are unsupported by Qt.
- `ui/components/filter_sidebar.py:31`: unused `filterSidebarModel` context property (overwritten across sidebars on the shared engine).
- `ui/components/icon_button.py`: unused `set_icon_color` and `_primary`.
- `ui/panels/mod_list_panel.py`: `setCompactMode`, `commitOrder` slots; `ui/models/mod_list_model.py`: `activeCount` property, `active_count`.
- `core/services/update_service.py`: `parse_version` has no production caller.
- `_app.py:189-203`: `steam_enabled` is never read; `downloaders: list[str]` duplicates it. Catch `ModuleNotFoundError` with `exc.name == "pxsteamdl"` instead of `ImportError`.
- `ui/components/view_rail_panel.py` hardcodes 48/220, duplicating `RAIL_*` constants; `Theme.railMinWidth/railMaxWidth/sidebarWidth` have no consumers.
- `docs/UX_AUDIT.md` cites deleted `Sidebar.qml` / `preset_combo`.
- `docs/proposals/organizer-standard-rules.py`: 1842-line `.py` not covered by lint or tests; move to data or apply to `defaults.py`. `docs/mockups/startup-impact.html` is already implemented.
- `tests/test_plugin.py` still uses `workshop_download` as sample plugin name.
- `_app.py:133`: `setWheelScrollLines(8)` overrides the OS setting; unverified that Qt Quick honours it.

### Design and DI
- `core/providers/local.py:20-48`: module-level `_in_flight_scans` violates constructor DI; add a done-callback to retrieve the exception after `shield`.
- `core/providers/base.py:95`: eager `published_file_id` warm-up is unprofiled; wrap in `tm("warm_pfid")`.
- `ui/window/main_window.py`: `UpdateService` built inside the window; `_on_downloads_finished` re-calls `download_manager(self._ctx)`; `_downloads_refresh` not cancelled on close; `_check_for_updates` catches only `UpdateCheckError`; `_confirm_close` resets its guard before the save step.
- `ui/panels/mod_list_panel.py:81-84,138`: `getattr`/`hasattr` guards exist only for test fakes; use `ctx.config` / `ctx.config_changed` and fix the fake.
- `ui/panels/settings_panel.py`: rules path uses global `config_dir()` instead of `ctx.config_service.config_dir`; `cacheAvailable` gated on `paths.config_folder`; backend created before the QQuickWidget (teardown `TypeError` warnings); hidden `ProgressDialog` not deleted on failure.
- `core/services/activation_service.py`: `_build_edges` rebuilds a full `ConstraintGraph` per `apply()` (~180 ms at 3000 mods); `_strongly_connected` duplicates Tarjan in `graph.py`; `_Placement` does its work in `__init__`.
- `ui/panels/sidebar_panel.py`: `select_all_entry` reaches into the QML tree; add `FilterSidebar.set_current_index`.
- `ui/components/header_controller.py`: `_STRATEGY_LABELS[s]` raises `KeyError` for new `LaunchStrategy` members; inconsistent `downloadsBusy` setter/slot.
- `ui/components/dialogs.py:await_dialog`: no `try/finally`, so cancellation leaves the dialog alive; `deleteLater` on a deleted parent can raise.

### Behaviour
- `ui/panels/ModList.qml:122-176`: drag proxy ignores compact mode and its offsets do not match the delegate.
- `ui/models/mod_list_model.py:387`: `set_check_states` `dataChanged` does not cover inactive rows above the toggled one (stale "Inactive (N)").
- `ui/panels/startup_impact_data.py`: per-mod and per-phase tooltips label metrics differently; base-game off-thread time is counted in the summary but not in the estimate.
- `core/services/update_service.py`: `rc10` is not newer than `rc9`; 404 from `/releases/latest` reports an error instead of "up to date"; redirects are not followed.
- `core/checker/checker.py:161`: `_run_checker` caches `[]` after an exception for order-independent checkers until the next `rebuild()`.
- `core/support/report.py`: home-dir replacement is case-sensitive on Windows.
- `ui/components/procedural_preview.py`: `initials()` gives `1C` for `Combat [1.5]`, `RS` for `Rocket's Bar`; CamelCase split ignores non-ASCII; cached `QImage` is shared and mutable.
- `ui/window/actions.py`: `&Save Mod List` and `&Settings…` share the `S` mnemonic.
- Header and `ModInfo.qml:514` use the attached unthemed `ToolTip`; `PxButton.qml:48` tooltip `visible` reads the tooltip's own `text`; `PxDialogFooter` hardcodes bottom radius.
- `ui/panels/ModInfo.qml:316`: description re-parsed on every resize.
- Restore snapshot list shows no file name, times in UTC.

### Tests
- No test for `Activation` ceilings through batch chains, input-order stability of unrelated candidates, case-insensitive ids, or non-`AboutXmlMod` candidates.
- No tests for `UpdateDialog`, `mod_updates.update_state`/`updatable_ids`, `await_dialog`, `KeyboardShortcutsDialog`, `_close_like_escape`, 404/403/redirect in `UpdateService`, multi-digit `rc`.
- `test_generate_preview_is_thread_safe_image` does not test thread safety; toast/progress tests rely on `qWait` and private attributes.
- `test_startup_impact_data.py::test_tooltip_sums_metrics_sharing_a_label` pins the private `_metrics_by_phase`.
- `test_menu_bar.py::test_menu_actions_carry_shortcuts_and_fire_while_menu_bar_hidden` needs a review (see reviewer output).
- Each UI test file redefines its own `qapp` fixture; move to `conftest.py`.
- No CI job covers the no-`pxsteamdl` path (lazy import in `_app.py`).
- `test_downloads_model` pins "cancelled counts as failed".
