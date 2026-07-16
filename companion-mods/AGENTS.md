# companion-mods — Agent guide

## What this is

C# RimWorld mods for PxModRim, **not** the Python app itself. Two sub-projects:

- **PxLoadingProgress** (`ru.pyxiion.modrim.loadingprogress`) — a Harmony-patching RimWorld mod that adds a detailed loading-progress window. Dependencies (`Common.props`/`.targets`, `globalusings.cs`) come from sibling `rimworld-utils/`.
- **rimworld-utils/** — shared C# build infrastructure (`.props`, `.targets`, `build.sh`, `bump_version.sh`), not a mod.

The parent repo `RimSort/` has its own `AGENTS.md` for the Python/PySide6 app.

## Key files per mod

| File | Purpose |
|---|---|
| `Directory.Build.props` | Version prefix (`<VersionPrefix>`), imports `rimworld-utils/Common.props` |
| `About/About.xml` | Mod metadata, packageId, supportedVersions, modVersion |
| `LoadFolders.xml` | Maps RimWorld versions to per-version asset folders (e.g. `1.6/`) |
| `Source/<ModName>/<ModName>.csproj` | Actual C# project with `<Publicize>` items for internal RimWorld members |
| `<ModName>.sln` | Solution file |
| `bump_version.sh` | Bumps version in `Directory.Build.props`, `About.xml`, and `CHANGELOG.md`, then commits + signs a tag |

## Build

```bash
# All commands from PxLoadingProgress/ directory:
just build       # Compile (output lands in 1.6/Assemblies/)
just zip         # Build + package into PxLoadingProgress.zip
just clean       # Remove build artifacts and package output
```

Or manually:

```bash
dotnet build --property "RimWorldVersion=1.6" ru.pyxiion.modrim.LoadingProgress.sln

# Full copy-to-game (needs .vscode/build_config.sh):
../rimworld-utils/build.sh 1.6
```

`rimworld-utils/Common.targets` adds T4 template transformation (`dotnet-t4`) as a pre-compile step.

## Toolchain quirks

- **.NET 10 SDK** (`global.json` pins `10.0.0`, `rollForward: latestMajor`)
- **net481** target, `LangVersion latest`, `Nullable enable`
- **Global usings**: Verse, RimWorld, UnityEngine, HarmonyLib, System.* — injected from `rimworld-utils/globalusings.cs` via Common.props, not `ImplicitUsings`. **Do not add redundant `using Verse;` etc.**
- **Krafs.Rimworld.Ref** for RimWorld assembly references (versioned per RW version)
- **Krafs.Publicizer** to access `internal` RimWorld members (see `<Publicize>` in .csproj)
- **Harmony 2.3.6** (for RW 1.6) via `Lib.Harmony` NuGet
- **EditorConfig** is extensive (1130 lines): `file_scoped` namespaces, `var` everywhere, `expression_bodied` methods, CA rules as `warning`. Use `dotnet format` or let the IDE enforce.

## CI

Reusable workflows from `ilyvion/rimworld-utils`:
- `.github/workflows/build_mod.yml` — builds for each RW version, uploads artifact with `About/`, `Common/`, version folder, `LoadFolders.xml`, licenses
- `.github/workflows/release_mod.yml` — publishes Steam Workshop release

## Mod structure (output artifact)

```
About/          # About.xml + icons/fonts (excluded: *.pdn, *.svg, *.ttf)
Common/         # Shared assets (Languages/, Textures/)
1.6/Assemblies/ # Built .dll per RW version
LoadFolders.xml
CHANGELOG.md, LICENSE*, README.md
```

## Versioning

Update via `bump_version.sh <new-version>` (bumps `Directory.Build.props`, `About.xml`, `CHANGELOG.md`, creates signed git tag). Requires clean staged tree.

## Deeper context

For GABS launch workflow, writing in-game tests (RimTest Redux), and illustration pipeline see `rimworld-utils/CLAUDE.md`, which is written for the ilyvion RimWorld mod family that this mod is part of.

## What's absent

- No test project in this solution (no `*.Tests` project)
- No `ilyvion.Laboratory` dependency for this mod (`UseLaboratory` is `false`)
- No Qt, qasync, or Python tooling — those belong to the parent `RimSort/` project
