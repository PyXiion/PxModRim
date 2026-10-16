# PxModRim — Agent guide

## Workflow
1. `just format` — clang-format the C++ tree (fastest cycle)
2. `just check` — `format-check` → `lint` → `build` (`build` runs `build-js` first)
3. `just test` — build, then run the full ctest suite offscreen
4. `just ci` — `check` then `test`
5. Always make commits on the go — atomic, conventional commits as milestones are reached; never leave uncommitted work.

CI runs configure, build and test on every OS (see CI matrix). Only the Linux SteamWorkshop job also runs `just format-check` and `just lint`, so run `just check` before pushing.

## Environment
- Task runner: **just** (`just` lists the recipes). Node.js 22 for `build-js` (`npx esbuild`).
- **C++23** compiler: GCC or Clang (CI also builds with MSVC on Windows).
- **CMake >= 3.24** with **Ninja** (the presets use it). **ccache** is picked up automatically when it is on `PATH`.
- **Qt 6** (`find_package(Qt6 6.8)` is the minimum; CI and releases use **6.11.0**). Components: Core, Concurrent, Network, Sql, Gui, Quick, QuickControls2, QuickWidgets, Svg, Widgets, Test, Qml. `WebEngineQuick` is needed only by the SteamWorkshop variant.
- **QCoro 0.13** (`QCoro6`: Core, Network, Qml, Quick), built against the same Qt install. Enable coroutines with `qcoro_enable_coroutines()`, as the root CMakeLists already does.
- **pugixml**, **libzip**, **spdlog** via `find_package`.
- **CPM** (`cmake/CPM.cmake`) fetches glaze 5.5.4, Catch2 3.8.1 (tests) and PxSteamDL 0.3.0 (`cmake/Steam.cmake`, skipped by NoWorkshop).
- Without PxSteamDL the app runs as a plain mod sorter.

## Entrypoint
- `just run` (Steam browser variant, `build/default`), `just run-native` (native Workshop browser, `build/native`) or `just run-none` (sorter only, `build/none`). Each configures its own build directory with the matching `PX_WORKSHOP_VARIANT`, builds, and starts the app with `PX_LOG_LEVEL=debug`; extra arguments are passed to the app.
- Chain: `src/app/main.cpp` `main()` → `runApp()`, the composition root:
  config services → `CoreContext::create` → `px::modrim::registerCorePlugins` → `px::modrim::AppContext` → `px::modrim::registerUiPlugins` → `setupAll()` → `MainWindow` → `refreshMods()` → `initAll()`.
- `src/app/plugins.cpp` is the only list of concrete plugins.

## Module layout
```
src/
├── core/        pxcore (static lib, no GUI): domain logic, services, downloads, workshop, organizer, plugin API
├── ui/          pxui (static lib) + QML module PxModRim: Qt Quick views, widgets, plugins/
└── app/         pxmodrim executable: main.cpp (composition root), plugins.cpp (plugin list)
tests/
├── core/        Catch2 → pxcore_tests (mirrors src/core/)
└── ui/          Catch2 + Qt Test → pxui_tests (shared QApplication in main.cpp)
packaging/       desktop file, icons, RPM spec, Flatpak manifest, NSIS script
scripts/         build-inject-js.py
legacy/          read-only Python reference of the previous implementation
```

**Core must never know about UI.** `pxcore` links only non-GUI Qt modules (Core, Concurrent, Network, Sql) and must not include `ui/` headers. Check both with `grep -rn '#include "ui/' src/core tests/core` and `cmake --preset default --graphviz=build/graph.dot` (the target graph must show no `pxcore` → `Qt6::Gui`/`Quick`/`Widgets` edge). Plugins must not know about layers; layers must not know about plugins.

## Download plugins
Downloads are plugins; the UI only talks to `core/downloads/manager.hpp` (`DownloadManager`, always present, plugin name `downloads`, `kDownloadsManagerName`).
- Sources subclass `px::modrim::Downloader` (`core/downloads/downloader.hpp`). `SteamDownloader` (`core/downloads/steam.hpp`, plugin name `steam_downloader`) is the only one today. The constructor hands the source to the manager, and `setup()` registers it. Third-party includes stay inside the source's own `.cpp`, behind `PX_HAS_STEAM`. `registerCorePlugins` (`src/app/plugins.cpp`) adds it only when it is not disabled.
- Catalog: `WorkshopCatalog` (`core/workshop/catalog.hpp`, plugin `workshop_catalog`, depends on `downloads`) consumes the read-only catalog API through `core/workshop/client.hpp` with an injected configuration and client. It resolves transitive download plans and delegates files to `DownloadManager`, without activating mods. `CatalogSettings::url` (legacy `config.json` key `workshop_catalog_url`) defaults to `https://api.modrim.pyxiion.dev`; an empty string disables catalog network requests. Defaulted settings fields load from older files without a schema migration.
- UI plugins: `DownloadsUiPlugin` (`ui/plugins/downloads/`, Downloads tab, source-agnostic, shown when any downloader exists), `SteamWorkshopUiPlugin` (`ui/plugins/steam_workshop/`, Steam WebView, needs `steam_downloader`), `workshop::WorkshopUiPlugin` (`ui/plugins/workshop/`, native QML Workshop browser, needs `workshop_catalog` and `downloads`).
- Disable at runtime: `PX_DISABLED_PLUGINS=steam_downloader,steamworkshop,downloads_ui,workshop_catalog,workshop_ui` (comma-separated). Disabling the catalog also omits its UI.
- Installed catalog state uses discovered mods' `publishedFileId()` (About/PublishedFileId.txt or numeric folder); only downloader-managed local copies have known sync times. Steam-client Workshop copies are installed, but cannot be marked outdated without a known sync timestamp. Downloading places mods in Local mods and never subscribes or activates them.
- Catalog requests batch installed metadata by 100 IDs (`kIdsPerBatch`) and resolution by 50 mod roots / 10 collection roots (`kModIdsPerResolve`, `kCollectionIdsPerResolve`); large Update all selections retain dependency-first order and deduplicate shared dependencies across requests.
- New source: subclass `Downloader`, register it in `registerCorePlugins` (`src/app/plugins.cpp`), and add its name to the `downloaders` list; the UI needs no change.

## Plugin API
Core and the shell (`ui/window`, `ui/panels`) never name a concrete plugin; only `src/app/` does.
- Base classes: `px::modrim::Plugin` (`BasicPlugin<CoreContext>`) and `px::modrim::UiPlugin` (`BasicPlugin<AppContext>`), from `core/plugin.hpp` and `ui/context.hpp`. Override `setup(Ctx&)` (synchronous, dependency order), `QCoro::Task<void> init(Ctx&)` (awaited right after setup) and `shutdown()` (reverse order). Dependencies are passed to the constructor; the registry rejects unknown ones. `registerPlugin` with an existing name replaces the plugin in place.
- Settings: `PluginConfig<T>(&ctx.configService(), "name", {{"old_config_key", "field"}})` in `setup()` → `plugins/<name>.json`. Read `value()`; write with `update(T)`; subscribe to `changed`. The legacy map seeds the file the first time it is created.
- Settings UI: a UI plugin calls `app.addSettingsSection(factory)` (one Settings tab each). Subclass `PluginConfigSection<T>` for a typed settings struct, or `PluginConfigSectionBase` (`ui/settings_section.hpp`) with `title()` and `source()`. The QML root has `required property QtObject section`, reads `section.initial` and calls `section.set(key, value)`. Edits apply on Save only.
- Views: `RailView` subclasses declare `static constexpr RailViewMeta kRailMeta{viewId, iconName, label}` (optionally `listsDownloads = true` for the download queue) and are registered with `app.addRailView<V>()` in `registerUiPlugins`, before the plugins that read the rail. `openRoute(const QStringList&)` handles `modrim://<view_id>/…`.
- `SteamDownloaderUiPlugin` (`ui/plugins/steam_downloader/`) holds the Steam download settings section.

## Build variants
- `PX_WORKSHOP_VARIANT` (CMake cache, `NoWorkshop | SteamWorkshop | NativeWorkshop`, default `SteamWorkshop`): `cmake --preset default -DPX_WORKSHOP_VARIANT=NativeWorkshop`.
- Every build writes `workshop-variant.txt` beside the executable (build tree via a POST_BUILD copy; install via `install(FILES)`). `px::modrim::parseDisabledPlugins` (`src/app/plugins.cpp`) turns it into disabled plugins, on top of `PX_DISABLED_PLUGINS`. A missing file bakes nothing.
- `NoWorkshop` skips PxSteamDL (no `PX_HAS_STEAM`, no Steam downloader), omits the Downloads tab and both browsers, and never looks for WebEngine.
- `SteamWorkshop` adds the WebEngine browser and omits the native browser.
- `NativeWorkshop` omits the web browser and WebEngine.
- Code uses the compile-time gates, not variant names: `PX_HAS_STEAM` (public define of `pxcore` when PxSteamDL is found) and `PX_HAS_WEBENGINE` (public define of `pxui` when `Qt6::WebEngineQuick` is found, which only happens for SteamWorkshop).
- CI builds and tests all three variants on every OS; the release workflow packages all three.

## Code style
- All code lives in `px::modrim`, with sub-namespaces such as `px::modrim::workshop` and `px::modrim::providers`. `px::core`, `px::ui` and `px::app` no longer exist.
- Headers are `.hpp` with `#pragma once`; sources are `.cpp`. Namespaces are `lower_case`.
- Names are enforced by `.clang-tidy` (`readability-identifier-naming`): types, enums and concepts `PascalCase`; functions, methods, variables and parameters `camelCase`; private and protected data members `m_camelCase`; public struct fields `camelCase`; constants and enumerators `kPascalCase`; macros `UPPER_CASE`.
- Wire structs (JSON through glaze) keep `snake_case` fields, because those fields are the JSON keys. Wrap them in `// NOLINTBEGIN(readability-identifier-naming)` and `// NOLINTEND(readability-identifier-naming)`.
- Names that a framework fixes keep the spelling it requires: coroutine awaiter methods (`await_ready`, `await_suspend`, `await_resume`), glaze metadata (`glz::meta<T>` with `static constexpr auto value`), and Qt virtuals and macros.
- QML-facing `Q_INVOKABLE` and `Q_PROPERTY` names are camelCase. A QML signal handler is `on` plus the C++ signal name (`void changed();` is handled by `onChanged`), so the two spellings must match.
- `just format` applies `.clang-format` (Google base, 2-space indent, 80 columns, includes grouped as std, Qt, other libraries, project); `just format-check` verifies without rewriting.
- `just lint` runs `run-clang-tidy` over the `src` and `tests` sources in `build/default/compile_commands.json` (configuring first if needed); `just lint-fix` applies the renames.

## Key conventions
- C++23, `.clang-format` (Google base, 2-space indent, 80 columns; see Code style). The root sets `-Wall -Wextra -Wpedantic`.
- No comments unless explaining _why_. _What_ comments allowed for long blocks.
- Never global singletons — constructor DI everywhere.
- Never orphan Qt objects — always pass a parent.
- Errors: functions return `Result<T>` (`std::expected<T, Error>`); use `std::unexpected(Error{...})`.
- Logging: spdlog. Log file `logs/pxmodrim.log` under the config directory; `PX_LOG_LEVEL` sets the level.
- Settings are JSON through glaze (`ConfigService`, `PluginConfig`): `config.json` and `plugins/<name>.json` in the config directory.
- Git renames: `git mv`.
- Ask the user before picking a Qt/QML widget type.
- Always make commits on the go — never leave the working tree uncommitted at milestones or task completion.

## Coroutines
- Async functions return `QCoro::Task<T>`. Wait with `co_await`:
  - network replies: `co_await qCoro(reply).waitForFinished()`
  - signals: `co_await qCoro(obj, &Class::signal)`
  - modal dialogs: `co_await awaitDialog<T>(args...)` (`ui/components/dialogs.hpp`), never `exec()`
  - delays: `co_await QCoro::sleepFor(duration)`
  - QML-invokable coroutines return `QCoro::QmlTask`
- Long blocking work runs on a pool (`px::modrim::providers::runOnPool` or `QtConcurrent::run`), and the coroutine `co_await`s the future.
- Never `QApplication::processEvents()`, `QDialog::exec()`, `QThread`, `sleep`/`sleep_for`, or `QTimer::singleShot(0, …)`. Wait for the event that matters instead of spinning or deferring by zero.
- `QCoro::waitFor` belongs only at the top of `main()`.
- GCC 16 ICE (`gimple-low.cc`, `lower_stmt`) was hit by a coroutine with `co_await` in both arms of a `?:`; write it as `if`/`else` assigning a local.

## TypeScript / JS build
Steam Workshop WebView injection uses TypeScript sources in `src/ui/plugins/steam_workshop/ts/`, bundled to `src/ui/plugins/steam_workshop/inject.js` by `scripts/build-inject-js.py`, which runs:

```
npx --yes esbuild ts/main.ts --bundle --format=iife --outfile=inject.js --target=es2020
```

- `just build-js` runs before `build`, `build-release`, `check` and therefore `test`. It is mtime-cached and rebuilds only when `ts/` changes.
- `inject.js` is gitignored. When it exists, `src/ui/CMakeLists.txt` embeds it as a resource (`plugins/*/inject.js` is globbed with `CONFIGURE_DEPENDS`). Without it the web browser remains usable but has no injected download controls.

## Testing
- Catch2 3.8.1 (CPM). `tests/core` → `pxcore_tests`, `tests/ui` → `pxui_tests`. `catch_discover_tests` registers every case with ctest.
- `tests/ui/main.cpp` owns the single `QApplication` for the UI binary (offscreen). Qt allows one per process.
- Run everything: `just test` (`QT_QPA_PLATFORM=offscreen ctest --preset default`). One case: `build/default/tests/pxui_tests "case name"`, or `ctest --preset default -R <regex>`.
- The test tree mirrors `src/`. Fake sources subclass `Downloader` (see `FakeDownloader` in `tests/core/downloads/test_manager.cpp`); providers are mocked by subclassing `providers::BaseModProvider`.
- Tests that need a variant feature are wrapped in `#ifdef PX_HAS_STEAM` / `PX_HAS_WEBENGINE`, as `tests/core/downloads/test_steam.cpp` is.

## QML / SVG quirks
- QML files sit directly in `src/ui/` (the module root, URI `PxModRim`). Qt does not reliably register types from subfolders, and plain type names resolve only within one flat module.
- Resource paths: `:/qt/qml/PxModRim/theme/style.qss`, `:/qt/qml/PxModRim/assets/*`, `:/qt/qml/PxModRim/plugins/<name>/inject.*`.
- Icons come from `image://icons/<name>?color=<hex>`, served by `SvgIconProvider` (`ui/components/svg_provider.hpp`) on the shared `QQmlEngine`.
- **Color URL encoding**: QML must `encodeURIComponent(color)`. The provider decodes with `QUrl::fromPercentEncoding`.
- `main.cpp` keeps `Q_IMPORT_QML_PLUGIN(PxModRimPlugin)` and sets `QT_QUICK_CONTROLS_STYLE=Basic` by default.

## Companion mods
C# RimWorld mod submodules (see `companion-mods/AGENTS.md`):
- `companion-mods/PxLoadingProgress` — Harmony-patching loading progress mod
- `companion-mods/rimworld-utils/` — shared C# build infrastructure (from ilyvion/rimworld-utils)

## Other source trees
- `cf-workers/` — Cloudflare Workers (Steam Workshop dependency resolver, separate deploy); checked by the `workshop-backend` CI job.

## CI matrix
- `ci.yml`: ubuntu-latest, windows-latest, macos-latest × NoWorkshop, SteamWorkshop, NativeWorkshop. Each job installs Qt 6.11.0 (`jurplel/install-qt-action`, WebEngine modules only for SteamWorkshop), builds QCoro v0.13.0 from source against that Qt, runs `just build-js`, then `cmake --preset default` → `cmake --build --preset default` → `ctest --preset default`.
- Linux CI installs `libegl1 libopengl0 libxcb-cursor0 libxkbcommon-x11-0` plus pugixml, libzip and spdlog dev packages. Windows uses vcpkg triplet `x64-windows-static-md`.
- The Linux SteamWorkshop job alone runs `just format-check` and `just lint` after configure, installing `clang-format` and `clang-tidy` from apt first.
- Qt tests need `QT_QPA_PLATFORM=offscreen`; CI sets it for every job.

## Packaging
Release builds run on `v*` tags (`build.yml`). Each variant runs `cmake --build --preset release` and `cmake --install`, then:
- **Linux**: install into `dist/AppDir/usr`; linuxdeploy + linuxdeploy-plugin-qt write the AppImage and deploy Qt into the same tree; `PxModRim-Linux-<variant>.tar.gz` is `usr/`; `.deb` (`/opt/pxmodrim`, `/usr/bin/pxmodrim` symlink) and `.rpm` (`packaging/linux/pxmodrim.spec`) are built from the same tree. The Flatpak manifest (`packaging/flatpak/`) consumes the tarball.
- **Windows**: install into `dist/PxModRim/bin`, rename to `PxModRim.exe`, `windeployqt --release --no-translations --qmldir src/ui`, then the NSIS installer (`packaging/windows/installer.nsi`) and the release zip.
- **macOS**: install as `pxmodrim.app`, rename to `PxModRim.app`, `macdeployqt -qmldir=src/ui`, `dylibbundler` for Homebrew dylibs. Developer ID signing and notarization run only when the secrets are set; otherwise the app is ad-hoc signed.
- The version comes from the tag for artifact names; keep `project(VERSION)` in the root CMakeLists in step with it.

## AI PRs
PR description starts with: "Greetings, PyXiion! The silicon-based contributor is here."
PR title starts with: "🦀"
Omission = auto-rejected.
