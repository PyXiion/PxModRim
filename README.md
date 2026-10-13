<p align="center">
  <img src="src/pxmodrim/ui/assets/logo_nobg_4.svg" alt="PxModRim" width="240">
</p>

A friendly, modern mod manager for RimWorld. NOw with fast Steam downloading

<p align="center">
  <img src="https://img.shields.io/badge/python-3.12-blue?logo=python" alt="Python 3.12" />
  <img src="https://img.shields.io/badge/license-LGPL--3.0-blue" alt="License: LGPL-3.0" />
  <img src="https://img.shields.io/codacy/grade/f0c8db5021594e89adf514192fa9a205" alt="Codacy grade" />
  <img src="https://img.shields.io/badge/platform-linux%20%7C%20win%20%7C%20macOS-lightgrey" alt="Platforms" />
</p>

## Downloads

Choose a version, then download it for your system:

| Version | What you get | Windows | Linux | macOS |
|---|---|---|---|---|
| **NativeWorkshop** | Mod manager with a native Workshop browser and mod downloader. | [Installer](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-Windows-NativeWorkshop-Setup.exe) | [AppImage](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-Linux-x86_64-NativeWorkshop.AppImage) | [DMG](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-macOS-NativeWorkshop.dmg) |
| **SteamWorkshop** | Mod manager with the familiar Steam web browser and mod downloader. | [Installer](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-Windows-SteamWorkshop-Setup.exe) | [AppImage](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-Linux-x86_64-SteamWorkshop.AppImage) | [DMG](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-macOS-SteamWorkshop.dmg) |
| **NoWorkshop** | Just sorting and organizing mods, without Workshop browsing or downloading. | [Installer](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-Windows-NoWorkshop-Setup.exe) | [AppImage](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-Linux-x86_64-NoWorkshop.AppImage) | [DMG](https://github.com/PyXiion/PxModRim/releases/latest/download/PxModRim-macOS-NoWorkshop.dmg) |

[All downloads](https://github.com/PyXiion/PxModRim/releases/latest), including portable ZIPs, Linux packages and Flatpak.

---

![Screenshot](screenshot.png)

---

## What it does

- **Scans everything in one place** — Steam, local, and core mods all show up together.
- **Instant warm startup** — persistent SQLite metadata cache skips re-parsing unchanged mods.
- **Catches problems for you** — missing dependencies, load-order conflicts, and other issues appear right in the list.
- **Sorts your load order automatically** — one click and your active mods are ordered by dependencies and community
  rules.
- **Saves back to RimWorld** — writes your final mod list to `ModsConfig.xml` so the game sees exactly what you picked.
- **Safeguards your configuration** — automatic timestamped snapshots with one-click rollback if something breaks.

---

## Installation

Pre-built packages are generated automatically for tagged releases on GitHub:

- **Linux:** AppImage, `.tar.gz`, `.deb`, `.rpm`, and Flatpak (`com.github.PyXiion.PxModRim`)
- **Windows:** NSIS Installer (`.exe`) and portable `.zip`
- **macOS:** Application bundle `.zip` and `.dmg`

Download the appropriate package from the [Releases](https://github.com/PyXiion/PxModRim/releases) page for your platform.

Flatpak can access common Steam locations and the RimWorld config directory. If a Steam library is stored
elsewhere, grant that folder to PxModRim in Flatseal; the app does not receive broad host-filesystem
access.

macOS builds are ad-hoc signed by default. Developer ID signing and notarization are enabled only when
release signing secrets are configured; otherwise, macOS may require manual Gatekeeper approval.

If you prefer to run or build from source, see the developer notes in [`AGENTS.md`](./AGENTS.md).

---

## Roadmap / TODO

The checklist below tracks major feature areas at a glance. For the detailed, prioritised
engineering plan — release blockers, quality gates, milestone scope, and acceptance criteria —
see [`ROADMAP.md`](./ROADMAP.md).

- [x] Core mod discovery (Steam, local, core)
- [x] Responsive three-panel UI with sidebar filters
- [x] Dependency and conflict diagnostics
- [x] Automatic load-order sorting
- [x] Save active mod list to `ModsConfig.xml`
- [x] Game launching (Steam, standalone, with optional wrappers)
- [x] Steam Workshop integration (browse, subscribe, update)
- [x] Downloading Workshop mods without the Steam client (via [PxSteamDL](https://github.com/PyXiion/PxSteamDL), whose backend supports Linux, macOS, and Windows)
- [ ] Launch presets (mods/configs/etc)
- [ ] Player log viewer with filtering and colorization
- [ ] File search across all installed mods (maybe?)
- [ ] Custom sort rules editor
- [ ] Theme and appearance options
- [ ] Translations
- [ ] CLI

Features from RimSort will be adopted slowly and only when they genuinely improve the player experience.
---

## License

LGPL-3.0.
