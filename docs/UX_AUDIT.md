# PxModRim — User Experience (UX) Audit & 30 Improvement Proposals

**Document Status:** Proposal / UX RFC  
**Target Milestone:** 0.7.0 ("Make the manager ready for use") & 1.0.0  
**Inspection Date:** 2026-09-21  
**Scope:** Core desktop interface (`src/pxmodrim/ui/`), user workflows, interaction patterns, accessibility (A11y), and relevant GitHub issues.

---

## 1. Executive Summary & Methodology

PxModRim is designed around three core architectural tenets: **high responsiveness**, **zero frozen UI during scans/sorting**, and **clean separation of mod sources**. While the technical foundations (async I/O via `qasync`, sub-millisecond QML virtualized list, and instant dependency graph construction) excel compared to legacy managers like RimSort, the user-facing workflow currently exhibits friction common to early-stage desktop utilities:

1. **Passive state awareness:** The UI does not clearly communicate "dirty" (unsaved) states or pending background tasks.
2. **Missing defensive guardrails:** Launching the game bypasses diagnostic checks (e.g. broken dependencies or missing Core).
3. **Incomplete ergonomic loops:** Power-user actions (bulk enabling, right-click actions, keyboard-driven navigation, drag auto-scrolling) are either absent or partially implemented.
4. **Accessibility gaps:** Mod status badges and provider identities rely primarily on color coding and mouse-hover tooltips without accessible screen-reader metadata.

This document identifies **30 concrete UX improvements**, categorized into **Refinements to Existing UI** (quick wins and fixes to current widgets) and **Roadmap Subsystem UX** (features tracked in `ROADMAP.md` and GitHub issues).

---

## 2. Verified Facts & Architectural Grounding

To ensure every proposal is actionable and technically grounded, the following facts were verified in the codebase:

- **Launch auto-saves (`MainWindow._launch_game` in `main_window.py:321`):** Triggering game launch automatically calls `save_active_layout()`. However, if the config folder is invalid or missing, it emits a toast warning and still proceeds with launching. Crucially, it does **not** evaluate whether unresolved critical errors exist in `DiagnosticsService`.
- **Hardcoded header version (`Header.qml:38`):** The header text displays static `"v0.1.0"`, detached from `importlib.metadata` or `pyproject.toml`.
- **Fixed sidebar width (`ModsViewPanel` in `mods_view.py:50`):** `self.sidebar.setFixedWidth(240)` prevents users from resizing the panel on ultra-wide or compact displays; the panels are nested in a `QHBoxLayout` instead of a `QSplitter`.
- **Preset combo is currently a static placeholder (`sidebar_panel.py:31`):** `self.preset_combo.addItem("Default")` contains a single hardcoded entry without management signals or backing service (tracked in Issue #7).
- **Multi-select exists, but lacks dedicated bulk actions (`ModList.qml:144-162`):** The list supports Ctrl+click and Shift+click range selection, but the only batch interaction is `Return` (toggle checked).
- **Missing right-click context menu (`ModList.qml:129-222`):** The `MouseArea` only processes LeftButton mouse clicks and drags.
- **Accessibility metadata absent (`ModList.qml`, `Sidebar.qml`, `Header.qml`):** Components lack `Accessible.name`, `Accessible.role`, and `Accessible.description` properties (tracked in Issue #33).

---

## 3. The 30 UX Improvement Proposals

### Group I. Application State, Save Discipline & Launch Safety

#### 1. Visual "Unsaved Changes" Indicator & Save Button State
- **Code Context:** `Header.qml` (Save button at line 88) is perpetually enabled with identical styling. Neither `ModListModel` nor `ModsConfig` propagates a dirty-state flag to the header.
- **User Scenario:** A player reorganizes 10 mods and toggles several dependencies, then steps away to read a wiki article. Returning, they cannot tell whether changes were saved to disk.
- **Proposed Solution:**
  - Display a subtle badge/dot (`● Unsaved changes`) in the header when current active UUIDs or order differ from `ModsConfig.xml`.
  - Dim/disable the Save button when the list is clean, highlighting it when dirty.
  - Intercept window close events (`MainWindow.closeEvent`) to prompt: *"You have unsaved changes. Save before exiting? [Save & Exit] [Discard] [Cancel]"*.
- **Priority:** **P0**
- **Impact:** Eliminates accidental loss of carefully ordered modlists.

#### 2. Explicit "Save & Launch" Feedback & Hard-Stop on Save Failure
- **Code Context:** In `main_window.py:318-338` (`MainWindow._launch_game`), the application automatically calls `save_active_layout()` prior to launching. However, this workflow has two critical UX shortcomings:
  1. If `save_active_layout()` returns `False` (e.g. RimWorld config path is unconfigured or not writable), it only posts a transient warning toast (`"Config folder not set — mod list won't be saved"`) and **proceeds with game launch anyway**.
  2. Because the auto-save is silent and the separate "Save" button in `Header.qml` remains enabled, users are confused about whether they need to manually click Save before Play.
- **User Scenario:** A user with an unconfigured RimWorld config path clicks Play. The toast flashes and disappears while the game window starts up. The game loads purely vanilla because the modlist was never saved, leaving the user perplexed as to why none of their enabled mods appeared in-game.
- **Proposed Solution:**
  - **Hard-Stop on Failure:** If `save_active_layout()` fails, immediately abort the launch sequence and display a modal dialog: *"Cannot save modlist: Config directory not configured or inaccessible. Game launch aborted. [Open Settings] [Cancel]"*.
  - **Clear Feedback:** Communicate in the Play button tooltip that clicking Play commits the current active layout to disk, and display a unified toast: *"Saved 142 active mods and launching game..."*.
- **Priority:** **P0**
- **Impact:** Prevents misleading game launches with stale configurations and clarifies the relationship between the Save and Play actions.

#### 3. Pre-Launch Diagnostic Health Check
- **Code Context:** `MainWindow._launch_game` (`main_window.py:318-338`) does not consult `DiagnosticsService` before invoking `game_launcher.launch()`.
- **User Scenario:** A user accidentally disables `Harmony` or misses a required Core dependency. They click the prominent green "Play" button. RimWorld spends 5 minutes loading heavy assets only to crash to desktop with a black screen or broken UI.
- **Proposed Solution:**
  - Before invoking `GameLauncher.launch()`, inspect `DiagnosticsService` for critical errors.
  - If errors exist, present a blocking dialog: *"Critical mod errors detected (N). The game is likely to crash on startup. [Review Errors] [Launch Anyway] [Cancel]"*.
- **Priority:** **P0**
- **Impact:** Saves players minutes of frustrating load-and-crash cycles by catching fatal dependency issues before launch.
#### 4. Dynamic Application Versioning in Header
- **Code Context:** `src/pxmodrim/ui/components/Header.qml:38` contains hardcoded `text: "v0.1.0"`.
- **User Scenario:** Beta testers and contributors running nightlies or newer package versions submit bug reports citing "v0.1.0", confusing maintainers.
- **Proposed Solution:** Expose `pxmodrim.__version__` to QML through `HeaderController` or engine context.
- **Priority:** **P2**
- **Impact:** Eliminates misleading UI version strings and improves bug report accuracy.

---

### Group II. Mod List Ergonomics, Ordering & Direct Manipulation

#### 5. Explicit Load Order Index Display
- **Code Context:** `ModList.qml:275-300` displays name, package ID, and badges, but no numeric sequence index.
- **User Scenario:** Mod instructions frequently stipulate: *"Load this mod within the top 10 positions"* or *"Load exactly after Core (position 2)"*. Users must count list rows by hand.
- **Proposed Solution:**
  - Render an order index badge (`#1`, `#2`, `#3`...) to the left of the checkbox for active mods.
  - Display a dash (`-`) or mute the index for disabled mods to reinforce that inactive items have no load order precedence.
- **Priority:** **P1**
- **Impact:** Instant visual clarity for strict load order requirements.

#### 6. Right-Click Context Menu for Mods
- **Code Context:** `ModList.qml:129-222` handles left-click drag and selection only; right-clicks are ignored.
- **User Scenario:** A player wants to inspect a mod's local files, copy its Package ID for patching, or open its Steam Workshop page without switching panels.
- **Proposed Solution:**
  - Add a native context menu (`QMenu` / `Menu`) on right-click:
    - *Toggle Active (Space / Enter)*
    - *Move to Top / Move to Bottom*
    - *Copy Package ID*
    - *Copy Mod Name*
    - *Open Local Folder in File Manager*
    - *Open in Steam Workshop (if Steam mod)*
    - *Show Dependencies / Dependents*
- **Priority:** **P1**
- **Impact:** Drastically accelerates power-user workflows by eliminating panel switching.

#### 7. Floating Bulk-Action Bar on Multi-Selection
- **Code Context:** `ModList.qml` allows selecting multiple rows with Shift/Ctrl, but provides no multi-item action UI.
- **User Scenario:** A user selects 30 newly downloaded mods and wants to enable all of them at once, or disable an entire faction mod pack.
- **Proposed Solution:**
  - When `selectedIndices.length > 1`, display a floating bottom toolbar:
    `"32 mods selected | [Enable All] [Disable All] [Move to Top] [Move to Bottom] [Deselect]"`
- **Priority:** **P1**
- **Impact:** Makes existing multi-selection capabilities immediately useful.

#### 8. Undo / Redo for Mod List Changes
- **Code Context:** Issue [#23](https://github.com/PyXiion/PxModRim/issues/23). Reordering via drag-and-drop or clicking "Auto-sort" cannot be undone without manually reconstructing the list or restarting.
- **User Scenario:** A player who spent 20 minutes fine-tuning load order accidentally drags a mod into the wrong position or clicks "Auto-sort" by mistake.
- **Proposed Solution:**
  - Implement a `QUndoStack` capturing list mutations: item moves, toggles, and auto-sorting passes.
  - Bind standard shortcuts `Ctrl+Z` and `Ctrl+Y` (or `Ctrl+Shift+Z`), with brief action descriptions (e.g. *"Undo: Auto-sort mods"*).
- **Priority:** **P0**
- **Impact:** Eliminates fear of experimenting with mod load orders.

#### 9. Auto-Sort Diff & Confirmation Review
- **Code Context:** `HeaderController.autoSort()` immediately applies the sorted order via `SortService`.
- **User Scenario:** The player clicks "Auto-sort". The entire list rearranges instantaneously. The player has no idea what changed, what was fixed, or whether any manual overrides were moved.
- **Proposed Solution:**
  - Provide a summary toast or quick modal: *"Sorted 184 mods: 14 positions shifted, 3 dependency conflicts resolved. [View Diff] [Undo]"*.
- **Priority:** **P1**
- **Impact:** Fosters user trust in the automated sorting engine.

#### 10. Quick-Filter Pills Above the Mod List
- **Code Context:** `ModListPanel` only contains the `search_input` line edit (`mod_list_panel.py:77-81`). Category filtering requires traversing the sidebar.
- **User Scenario:** While browsing "All Mods", a user wants to view only active mods with errors in one click, without losing their search text.
- **Proposed Solution:**
  - Add a horizontal bar of toggle pills under the search field:
    `[All] [Active] [Inactive] [Errors] [Warnings] [Steam] [Local]`
- **Priority:** **P1**
- **Impact:** Enables rapid multi-dimensional filtering right at the point of focus.

#### 11. Search Query Syntax Support
- **Code Context:** `ModListProxyModel` filters by plain substring matches on name and package ID.
- **User Scenario:** A player wants to find all mods created by "Oskar Potocki" or inspect only local non-Steam mods.
- **Proposed Solution:**
  - Enhance proxy search filtering to parse structured tokens:
    - `author:<name>`
    - `package:<id>`
    - `source:steam` / `source:local`
    - `status:error` / `status:warn`
- **Priority:** **P2**
- **Impact:** Essential power feature for modpack authors managing hundreds of mods.

#### 12. Informative Empty States with Action Buttons
- **Code Context:** When a filter or search produces zero matches, `ModList.qml` displays an empty dark rectangle (`Theme.elevate1`).
- **User Scenario:** A player types a typo in the search box or selects "With errors" when no errors exist, and wonders if the application hung or failed to scan mods.
- **Proposed Solution:**
  - Display a centered state graphic and explanatory text:
    - Filter empty: *"No mods matching «...» [Clear Search]"*
    - Zero errors: *"No errors found! Your active modlist has clean dependency rules."*
    - Zero mods discovered: *"No mods found. Check your Steam & game paths in Settings. [Open Settings]"*
- **Priority:** **P1**
- **Impact:** Eliminates UI ambiguity and guides users toward resolution.

#### 13. Compact View Density Toggle
- **Code Context:** `ModList.qml:61,117` fixes row height at `52px`. On 1080p laptop displays, only 12-14 mods fit on screen at a time.
- **User Scenario:** Managing a 400-mod collection requires excessive scrolling.
- **Proposed Solution:**
  - Add a density toggle in Settings or header (Normal: 52px / Compact: 32px).
  - Compact mode reduces avatar size to 20x20, places name and package ID on a single line, and tightens vertical margins.
- **Priority:** **P1**
- **Impact:** Increases visible information density by 60% on smaller displays.

#### 14. Smooth Drag-and-Drop Auto-Scrolling
- **Code Context:** `ModList.qml:177-201` calculates `targetIndex` based on current viewport coordinates, with no auto-scroll trigger near list boundaries.
- **User Scenario:** A user drags a mod from position 150 toward position 1. Reaching the top edge of the list widget, the list does not scroll; the user must drop, scroll manually, and drag again.
- **Proposed Solution:**
  - When `dragProxy` is within 40px of the top or bottom edge of `listView`, activate a viewport scroll timer with velocity proportional to proximity to the edge.
- **Priority:** **P0**
- **Impact:** Resolves a major ergonomic flaw in manual mod ordering.

---

### Group III. Workspace Layout, Sidebar & Preset Ergonomics

#### 15. Resizable Panels via QSplitter
- **Code Context:** `ModsViewPanel` (`src/pxmodrim/ui/views/mods_view.py:50`) enforces `self.sidebar.setFixedWidth(240)`. The three main panels are arranged in a static `QHBoxLayout`.
- **User Scenario:** Users on high-resolution monitors (1440p / 4K) want to widen the mod list to prevent long mod titles from eliding, or expand the info panel to read lengthy mod descriptions.
- **Proposed Solution:**
  - Replace `QHBoxLayout` with a `QSplitter`.
  - Persist pane width ratios across sessions in `UIPrefs`.
- **Priority:** **P1**
- **Impact:** Adapts the workspace gracefully across laptop, desktop, and ultra-wide setups.

#### 16. Distraction-Free / Focus Mode (Collapsible Panels)
- **Code Context:** The sidebar and info panel occupy at least 540px combined width, leaving limited space for the mod list on low-resolution displays.
- **User Scenario:** A user on a Steam Deck or 13" laptop wants maximum horizontal space for sorting and comparing mod titles.
- **Proposed Solution:**
  - Provide collapse toggle buttons or keyboard shortcuts (`Ctrl+B` for sidebar, `Ctrl+I` for info panel).
- **Priority:** **P2**
- **Impact:** Accommodates compact mobile/handheld form factors.

#### 17. Interactive Preset Management in Sidebar
- **Code Context:** `SidebarPanel` (`sidebar_panel.py:29-32`) houses a static `QComboBox` with only `"Default"`. Issue [#7](https://github.com/PyXiion/PxModRim/issues/7).
- **User Scenario:** Players switch between a light vanilla-plus playthrough, a medieval overhaul, and a heavily modded sci-fi colony.
- **Proposed Solution:**
  - Attach management actions next to the preset dropdown: `[+] New Preset`, `[✎] Rename`, `[✕] Delete`, `[Duplicate]`.
  - Warn if switching presets while unsaved changes exist in the active profile.
- **Priority:** **P0**
- **Impact:** Delivers one of the most requested features for mod-heavy RimWorld players.

#### 18. Collapsible Filter Groups in Sidebar
- **Code Context:** `Sidebar.qml:23-47` renders sections as non-interactive header delegates.
- **User Scenario:** As additional diagnostic categories and mod providers are discovered, the sidebar becomes tall and cluttered.
- **Proposed Solution:**
  - Make section headers clickable to toggle collapse/expand (`▼ / ▶`), keeping frequently used filters in view.
- **Priority:** **P2**
- **Impact:** Maintains a tidy sidebar hierarchy as the application grows.

---

### Group IV. Diagnostics, Dependencies & Conflict Resolution

#### 19. Actionable 1-Click Fixes for Diagnostic Issues
- **Code Context:** In `mod_info_panel.py`, `IssueRow` (lines 57-86) renders an icon, category name, and static text detail. There are no interactive controls.
- **User Scenario:** An error badge states: *"Missing dependency: brrainz.harmony"*. The user must manually navigate to search, verify if Harmony is installed, download or activate it, and drag it to the top.
- **Proposed Solution:**
  - Add contextual action buttons directly inside `IssueRow`:
    - Installed but disabled dependency: `[Enable Mod]`
    - Incorrect load order: `[Move to Correct Position]`
    - Missing from local disk: `[Search in Workshop]` / `[Download via SteamCMD]`
    - Incompatible mod active: `[Disable Conflicting Mod]`
- **Priority:** **P0**
- **Impact:** Transforms passive diagnostic text into an intelligent, one-click troubleshooting system.

#### 20. Interactive Dependency Navigation Chips
- **Code Context:** Mod dependencies in `ModInfoPanel` are rendered as static labels.
- **User Scenario:** While inspecting a mod, the user sees `Vanilla Expanded Framework` in the dependency list and wants to check its load position and version.
- **Proposed Solution:**
  - Make dependency chips clickable: clicking jumps directly to that mod in the list, selects it, and scrolls it into view.
- **Priority:** **P1**
- **Impact:** Eliminates tedious manual searching when inspecting dependency chains.

#### 21. Visual Dependency and Conflict Graph
- **Code Context:** Issue [#26](https://github.com/PyXiion/PxModRim/issues/26). The underlying dependency graph exists in `pxmodrim.core.checker.graph`, but has no visual representation in the UI.
- **User Scenario:** A user encounters a complex multi-mod cyclic dependency or load-order conflict where three mods require each other in conflicting order.
- **Proposed Solution:**
  - Provide a modal or tab view rendering an interactive node graph showing nodes (mods) and color-coded edges (green for requirements, red for conflicts, blue for load-after).
- **Priority:** **P1**
- **Impact:** Untangles confusing cyclic ordering conflicts that are nearly impossible to decipher from linear text.

#### 22. Hierarchical Tree View in Dependent-Mods Dialog
- **Code Context:** Issue [#62](https://github.com/PyXiion/PxModRim/issues/62). `_DependentModsDialog` (`mod_list_panel.py:32-55`) displays a flat textual bullet list of affected dependents.
- **User Scenario:** Disabling a core library like `Vehicle Framework` affects 20 sub-mods. A flat text list obscures which sub-mod depends on what.
- **Proposed Solution:**
  - Render an interactive tree with checkboxes, allowing users to inspect the hierarchy and selectively decide which branch of dependents to disable.
- **Priority:** **P1**
- **Impact:** Transparent cascading operations that prevent accidental modlist breakage.

#### 23. Distinct Textual Provider Badges (Color-Blind Accessibility)
- **Code Context:** `ModList.qml:75,263` uses background box color (`providerColor`) on the avatar as the sole indicator of mod origin (Steam, local, core).
- **User Scenario:** Users with red-green or blue-yellow color vision deficiencies struggle to distinguish subtle provider tints on 36x36 pixel avatars.
- **Proposed Solution:**
  - Display an explicit text badge or distinct iconography (`[Steam]`, `[Local]`, `[Core]`) in the info panel and optionally as a pill in the list row.
- **Priority:** **P1**
- **Impact:** Universal accessibility compliance without relying solely on color hue.

---

### Group V. Accessibility (A11y), Keyboard Ergonomics & Localization

#### 24. Screen-Reader Support for QML Components
- **Code Context:** Issue [#33](https://github.com/PyXiion/PxModRim/issues/33). `ModList.qml`, `Sidebar.qml`, and `Header.qml` lack `Accessible` properties.
- **User Scenario:** Visually impaired users relying on screen readers (Orca on Linux, NVDA/Narrator on Windows) encounter generic container widgets with no accessible names, states, or roles.
- **Proposed Solution:**
  - Annotate delegates in `ModList.qml`:
    ```qml
    Accessible.role: Accessible.ListItem
    Accessible.name: model.name + (model.checkState === Qt.Checked ? ", active" : ", inactive")
    Accessible.description: "Load order position " + (index + 1) + (model.hasError ? ", has errors" : "")
    ```
- **Priority:** **P1**
- **Impact:** Brings the application in line with modern software accessibility standards.

#### 25. Non-Hover Access to Error and Warning Badges
- **Code Context:** In `ModList.qml:363-424`, diagnostic badge details (`errorTooltip`, `warningTooltip`) are revealed exclusively via mouse-hover `ToolTip`.
- **User Scenario:** Keyboard-only users navigate the mod list using arrow keys. They see error symbols (`✖`, `⚠`), but cannot hover with a cursor to inspect the error text.
- **Proposed Solution:**
  - Render error and warning summaries prominently in the focused mod's `ModInfoPanel`.
  - Support `F2` or `Alt+Enter` to open an issue inspector popup for the currently focused row.
- **Priority:** **P1**
- **Impact:** Ensures complete parity between mouse and keyboard diagnostics workflows.

#### 26. Comprehensive Keyboard Shortcuts Scheme
- **Code Context:** Issue [#35](https://github.com/PyXiion/PxModRim/issues/35). `ModList.qml:430-474` only binds `Up`, `Down`, `Shift+Up/Down`, `Return`, and `Ctrl+A`.
- **User Scenario:** Power users want to manage their modlist without constantly switching between keyboard and mouse.
- **Proposed Solution:**
  - `Ctrl+F` or `/`: Focus search input.
  - `Esc`: Clear search / clear selection.
  - `Space`: Toggle active check state on selected mods.
  - `Ctrl+Up` / `Ctrl+Down`: Move selected mod(s) up/down in load order.
  - `Ctrl+Shift+Up` / `Ctrl+Shift+Down`: Move selected mod(s) to top/bottom.
  - `Ctrl+S`: Save active layout.
  - `Ctrl+Shift+S`: Trigger auto-sort.
  - `F5`: Refresh mod discovery.
  - `F1` or `Ctrl+P`: Launch game.
- **Priority:** **P1**
- **Impact:** Dramatic speed-up for keyboard-centric enthusiasts.

#### 27. Internationalization (i18n) & Localization Support
- **Code Context:** Issues [#9](https://github.com/PyXiion/PxModRim/issues/9) and [#10](https://github.com/PyXiion/PxModRim/issues/10). All UI strings in Python and QML files are currently hardcoded English string literals.
- **User Scenario:** RimWorld has a massive international and Russian-speaking community. Non-native English speakers can find specialized modding jargon confusing.
- **Proposed Solution:**
  - Wrap user-facing strings in `self.tr(...)` (Python) and `qsTr(...)` (QML).
  - Configure Qt Linguist translation workflows (`.ts` / `.qm`) and introduce initial Russian translations.
- **Priority:** **P1**
- **Impact:** Broadens adoption across non-English speaking player bases.

---

### Group VI. Long-Term Subsystem UX / Roadmap Initiatives

#### 28. Modlist Snapshots & One-Click Rollback
- **Code Context:** Issue [#24](https://github.com/PyXiion/PxModRim/issues/24); `README.md` Roadmap.
- **User Scenario:** A player updates 40 mods or installs a complex mod suite, breaking a 200-hour savegame. They need to restore their exact working load order from yesterday.
- **Proposed Solution:**
  - Automatically create a timestamped snapshot before major mutations (bulk saves, auto-sort passes, preset switches).
  - Provide a "History / Snapshots" dialog listing past states with timestamps, mod counts, and a one-click `[Restore Snapshot]` button.
- **Priority:** **P1** (Roadmap)
- **Impact:** Complete peace of mind when maintaining long-running game saves.

#### 29. Integrated RimWorld Player.log Viewer & Analyzer
- **Code Context:** Issue [#8](https://github.com/PyXiion/PxModRim/issues/8). Currently, `menu_bar.py:36` only offers "Open Logs Folder".
- **User Scenario:** The game crashes during play. The user is forced to open a 60,000-line `Player.log` in an external text editor and manually parse Unity stacktraces.
- **Proposed Solution:**
  - Build an in-app log viewer window featuring:
    - Real-time file watching of `Player.log`.
    - Colorized log levels (Error = Red, Warning = Yellow).
    - Heuristic mod attribution (identifying which mod/Harmony patch triggered the exception).
    - `[Copy Cleaned Log for Discord/Support]` action button.
- **Priority:** **P1** (Roadmap)
- **Impact:** Eliminates the #1 pain point of troubleshooting modded RimWorld crashes.

#### 30. Drag-and-Drop Mod Installation from OS File Manager
- **Code Context:** Issue [#25](https://github.com/PyXiion/PxModRim/issues/25). `ModListPanel._qml` sets `setAcceptDrops(True)` (line 116), but drop event handlers for external file URLs are unimplemented.
- **User Scenario:** A player downloads a mod `.zip` from GitHub or an external community site. Instead of hunting through nested system directories (`~/.steam/.../RimWorld/Mods`), they drag the `.zip` directly into the PxModRim window.
- **Proposed Solution:**
  - Detect dropped zip files or directories, unpack to the local `Mods` directory, validate `About.xml`, trigger an automatic mod refresh, and highlight the newly installed mod in the list.
- **Priority:** **P2** (Roadmap)
- **Impact:** Drastically simplifies manual mod installations for non-Steam users.

---

## 4. Prioritization & Effort Matrix

| ID | Proposal | Priority | Complexity | User Impact |
|:---|:---|:---:|:---:|:---|
| **#1** | Unsaved changes dirty-state & close prompt | **P0** | Low | Eliminates accidental data loss |
| **#2** | Explicit Save & Launch UX & abort on save failure | **P0** | Low | Prevents launching stale/unconfigured setups |
| **#3** | Pre-launch diagnostic health check | **P0** | Medium | Prevents game crash cycles |
| **#8** | Undo / Redo for modlist mutations | **P0** | Medium | Enables risk-free experimentation |
| **#14**| Smooth drag-and-drop auto-scrolling | **P0** | Medium | Fixes critical ordering friction |
| **#17**| Interactive preset management in sidebar | **P0** | Medium | Delivers essential multi-profile workflow |
| **#19**| 1-Click actionable fixes in diagnostics | **P0** | Medium | Makes troubleshooting effortless |
| **#5** | Numeric load order index display | **P1** | Low | High visual clarity for mod placement |
| **#6** | Right-click context menu | **P1** | Medium | Major speed-up for frequent actions |
| **#7** | Floating bulk-action bar | **P1** | Medium | Unlocks multi-selection potential |
| **#9** | Auto-sort diff & confirmation review | **P1** | Medium | Builds user trust in automated sorting |
| **#10**| Quick-filter pills above list | **P1** | Low | Fast multi-dimensional filtering |
| **#12**| Empty states with action buttons | **P1** | Low | Prevents UI ambiguity and confusion |
| **#13**| Compact density view toggle | **P1** | Medium | Essential for laptop/compact screens |
| **#15**| Resizable panels via QSplitter | **P1** | Low | Workspace layout adaptability |
| **#20**| Interactive dependency navigation chips | **P1** | Low | Instant navigation across dependency trees |
| **#21**| Visual dependency/conflict graph | **P1** | High | Visual clarity on cyclic/complex issues |
| **#22**| Hierarchical tree in dependent-mods dialog| **P1** | Medium | Prevents accidental cascade breakages |
| **#23**| Distinct textual provider badges | **P1** | Low | Accessibility for color-blind users |
| **#24**| Screen-reader support in QML | **P1** | Medium | Inclusive software compliance |
| **#25**| Non-hover access to badge tooltips | **P1** | Low | Full parity for keyboard navigation |
| **#26**| Comprehensive keyboard shortcuts | **P1** | Low | High ergonomics for power users |
| **#27**| Internationalization & Russian localization | **P1** | Medium | Expands accessibility to global userbase |
| **#28**| Modlist snapshots & rollback | **P1** | Medium | Long-term savegame safety |
| **#29**| Integrated Player.log viewer & analyzer | **P1** | High | Unlocks easy crash diagnosis |
| **#4** | Dynamic application version in header | **P2** | Low | Accurate bug reporting metadata |
| **#11**| Search query syntax tokens | **P2** | Medium | Advanced search for modpack builders |
| **#16**| Distraction-free / Zen mode | **P2** | Low | Maximizes usable space on small screens |
| **#18**| Collapsible filter groups in sidebar | **P2** | Medium | Keeps growing sidebar organized |
| **#30**| Drag-and-drop zip installation | **P2** | Medium | Streamlines non-Steam mod installs |
