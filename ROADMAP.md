# PxModRim Engineering Roadmap

This document is the prioritised engineering plan for PxModRim. `README.md` keeps a short
feature-level checklist for quick scanning; this document is the detailed, dependency-aware,
acceptance-criteria-driven plan behind it. It reflects the state of the repository and the
`PyXiion/PxModRim` GitHub tracker as observed on 2026-09-21. Re-verify issue/PR/milestone state
before treating any of the numbers below as current.

## Current state snapshot (2026-09-21)

- Project version: `0.1.0` (`pyproject.toml`).
- Refreshed `origin/main` and local `main` have diverged: `git rev-list --left-right --count
  origin/main...HEAD` reports `7 6` — seven commits are upstream-only and six local commits are
  not pushed upstream. Neither ref is a strict ancestor of the other; reconcile them before push.
- The test suite is deterministic after P0.1: five final `just test` runs each reported
  `198 passed` with zero `Invalid image provider` and zero null-model `TypeError` lines; the
  raise-on-download guard also passed the full suite.
- The Ruff migration and first-party lint/format cleanup are complete: `ruff 0.16.8`,
  `just check`, full Ruff lint, and scoped format verification all pass.
- Seven Dependabot PRs remain pending refresh after the diverged refs are reconciled. The
  historical red run was an older Ruff-0.16-era base; the current code passes the local checks
  (`just check` exit 0, `just test` 198 passed on five consecutive runs, full `ruff check .`
  clean), but the working tree is not git-clean: 38 files are modified and 2 are untracked;
  the P0 work is uncommitted.
- The newest failing workflow on `main` is the scheduled `Codacy Security Scan` (SARIF rejected:
  `22 > 20` runs — see Phase P0.5).
- Milestone `0.7.0` ("Make the manager ready for use") is due `2026-08-31` — 21 days overdue as
  of today — and holds 14 open issues against 7 closed, mixing release blockers with
  speculative/low-priority features.

## Ordering principle

Work below is ordered **release blockers → product-core UX → packaging/release → convenience →
optional**, not by issue number. A phase does not start until the phase before it is done; within
a phase, tasks list their own dependencies explicitly. This ordering exists because release blockers
(deterministic suite, lint/format verification, QML runtime errors) had to be cleared before
anything else could be trusted; what remains open is the diverged-history reconciliation + PR
refresh (P0.2) and the Codacy SARIF incident (P0.5).

---

## Phase P0 — Green baseline (release blockers; nothing else starts before this)

### P0.1 — Make the test suite deterministic and network-free

**Goal:** complete. The flaky setup now creates the fake executable under the target prefix before
calling `ensure_installed()`, so the test exercises the installed short-circuit and persists the
new prefix without reaching the network. All SteamCMD download-capable tests mock
`_download_bytes`, and a temporary raise-on-download guard passed the full suite.

**Issue(s):** No issue filed yet — needs filing.

**Depends on:** nothing.

**Acceptance criteria:**
- Done — the fake executable is created under the target prefix before `ensure_installed()`.
- Done — all download-capable tests mock `_download_bytes` or the equivalent entry point; the
  raise-on-download full-suite guard also passed.
- Done — five consecutive earlier `just test` runs passed, and the final verification runs below
  each passed with 198 tests.

**Out of scope:** rewriting unrelated parts of `test_steam_cmd_service.py`; broader SteamCMD
service refactors; adding retry/backoff around the real network download (that would mask the
non-determinism rather than remove it from the test path).

### P0.2 — Refresh the 7 Dependabot PRs onto current `main`

**Goal:** the Ruff 0.16 migration and the UI fixes are complete in the working tree.
`pyproject.toml` requires `ruff>=0.16.0`, `uv.lock` resolves Ruff `0.16.8`, full Ruff lint and
format checks pass, `just check` exits 0, and three final `just test` runs each report 198 passed
with zero QML image-provider or null-model TypeError lines. The remaining P0.2 work is
maintainer-only: reconcile the diverged refs (`origin/main` is 7 commits behind local `main`,
which is 6 commits ahead), then refresh the Dependabot PRs.

**Issue(s):** No issue filed yet — needs filing.

**Depends on:** nothing, but refreshed `origin/main` and local `main` have diverged (`7 6`):
seven upstream-only commits include the Ruff `0.15.22 → 0.16.0` bump, while six local commits
are not pushed. The maintainer must reconcile this divergence before pushing and refreshing PRs.

**Acceptance criteria:**
- Done — `pyproject.toml` requires `ruff>=0.16.0`, `uv.lock` resolves Ruff `0.16.8`, and
  `uv run ruff --version` reports Ruff `0.16.8`.
- Done — `uv run ruff check --config pyproject.toml .` passes, and
  `uv run ruff format --check --config pyproject.toml src/ tests/ scripts/ packaging/` reports
  134 files already formatted.
- Done — `just check` exits 0 and three final `just test` runs each report 198 passed with zero
  `Invalid image provider` and zero `TypeError: Cannot read property` lines.
- Pending maintainer action — reconcile the seven upstream-only commits with local `main`'s six
  unpushed commits and push the resulting `main` to `origin/main`.
- Pending maintainer action — refresh all 7 Dependabot PRs onto the reconciled `main`, rerun CI,
  and inspect each platform result before merging.

**Out of scope:** `Codacy Security Scan` — tracked separately in P0.5; this task's acceptance
criteria concern the `ci` job only.

### P0.3a — Fix "Invalid image provider" in the Steam Workshop QML

**Goal:** complete. The Steam Workshop QML now uses the main window's `QQmlEngine` with the
registered `SvgIconProvider`, so all five referenced icons resolve through `image://icons/...`.

**Issue(s):** No issue filed yet — needs filing.

**Depends on:** nothing (independent of P0.1/P0.2).

**Acceptance criteria:**
- Done — all five Steam Workshop icons render through the registered provider.
- Done — three final `just test` runs produced zero `Invalid image provider` lines.

**Out of scope:** redesigning the icon system or `SvgIconProvider` itself; this is a wiring fix
(same engine/provider reaching the Steam Workshop QML tree), not a rewrite.

### P0.3b — Fix null `downloadQueueModel` in the download sidebar

**Goal:** complete. `DownloadSidebar.qml` now handles the empty queue without evaluating progress
properties on a null `downloadQueueModel`; the existing view preload test exercises the path.

**Issue(s):** No issue filed yet — needs filing.

**Depends on:** nothing (independent of P0.1/P0.2/P0.3a).

**Acceptance criteria:**
- Done — three final `just test` runs produced zero `TypeError: Cannot read property` lines.
- Done — the empty-queue progress UI degrades without null-model binding errors.

**Out of scope:** redesigning the download queue's data model or progress reporting semantics —
this is a null-guard/binding-order fix, not a feature change.

### P0.4 — Lint/format debt outside the CI-checked scope (not a release blocker)

**Goal:** complete. Ruff checks the full first-party tree (`src/`, `tests/`, `scripts/`, and
`packaging/`), while vendored `companion-mods/` and `rimsort-original/` are explicitly excluded
instead of being modified.

**Issue(s):** No issue filed yet — needs filing.

**Depends on:** nothing. **Not blocking:** this task does not gate P0.2's PR refresh or the start
of Phase P1.

**Acceptance criteria:**
- Done — `uv run ruff check --config pyproject.toml .` passes; vendored trees remain excluded.
- Done — `uv run ruff format --check --config pyproject.toml src/ tests/ scripts/ packaging/`
  reports 134 files already formatted, including `src/pxmodrim/_app.py`.
- Done — `just check` uses non-mutating Ruff lint and format-check recipes and exits 0.

**Out of scope:** widening `check`'s scope to arbitrary new directories beyond what already
exists in the repo; this closes the gap between what `check` claims to verify and what it
silently skips today, not a speculative expansion of lint targets.

### P0.5 — Fix the Codacy Security Scan SARIF rejection

**Goal:** `Codacy Security Scan` fails on all 7 open Dependabot PRs and is also the newest
failing workflow on `main` itself (scheduled run `35394001971`, 2026-09-18). The failure is
unrelated to Ruff or the SteamCMD test: `gh run view 35394001971 --log-failed` reports
`##[error]Code Scanning could not process the submitted SARIF file:` /
rejecting SARIF, as there are more runs than allowed (22 > 20). GitHub code scanning caps a
single SARIF upload at 20 analysis "runs"; Codacy's upload currently contains 22.

**Issue(s):** No issue filed yet — needs filing.

**Depends on:** nothing.

**Acceptance criteria:**
- The Codacy SARIF upload contains ≤20 runs (disable enough Codacy tools/categories to drop
  under the limit, or split the upload if Codacy's action supports multiple SARIF files).
- The `Codacy Security Scan` check goes green on `main` and on a refreshed Dependabot PR.

**Out of scope:** auditing which specific Codacy-flagged findings are true positives — this
task is about the SARIF upload mechanics, not triaging findings.

---

## Phase P1 — Quality gate you can actually measure

### P1.1 — Coverage tooling baseline

**Goal:** No coverage tooling exists today: `[dependency-groups] dev` in `pyproject.toml` lists
`nuitka`, `pydeps`, `pyright`, `pytest`, `pytest-asyncio`, `pytest-qt`, `pytest-xvfb`, and `ruff`,
but neither `pytest-cov` nor `coverage`; neither package is importable in the environment, and
`uv run coverage --version` fails outright. There is no `just coverage` recipe. Add the tooling and
produce a first baseline number before treating the 60% target as measurable at all.

**Issue(s):** No issue filed yet — needs filing (or track as a subtask under #40/#30).

**Depends on:** Phase P0 complete — a passing `just test` is required before a coverage number
means anything.

**Acceptance criteria:**
- `pytest-cov` (or `coverage`) is added to `[dependency-groups] dev` in `pyproject.toml`.
- A `just coverage` recipe exists in `justfile` and completes successfully.
- A baseline coverage percentage for `src/pxmodrim/core` is recorded (PR description or tracking
  issue).

**Out of scope:** hitting the 60% target itself — that is P1.2.

### P1.2 — [#40](https://github.com/PyXiion/PxModRim/issues/40) / [#30](https://github.com/PyXiion/PxModRim/issues/30) core coverage ≥60%

**Goal:** raise measured coverage of `src/pxmodrim/core` to at least 60%, enforced so it cannot
silently regress. #40 ("Core tests coverage. At least 60%.") and #30 ("Test coverage for core
modules") describe the same outcome and share the `0.7.0` milestone — flag them to the maintainer
as likely duplicates rather than tracking two separate deliverables for one number.

**Issue(s):** [#40](https://github.com/PyXiion/PxModRim/issues/40),
[#30](https://github.com/PyXiion/PxModRim/issues/30) (probable duplicates).

**Depends on:** P1.1.

**Acceptance criteria:**
- `just coverage` (or equivalent) reports ≥60% line coverage for `src/pxmodrim/core`.
- The same threshold is enforced as a CI check (the job fails below 60%) on all three OS matrix
  legs in `ci.yml`.

**Out of scope:** coverage for `src/pxmodrim/ui` — neither issue asks for it; do not silently
expand scope to the UI layer.

### P1.3 — [#31](https://github.com/PyXiion/PxModRim/issues/31) QML/UI testing infrastructure

**Goal:** build infrastructure so QML runtime errors (like the two fixed in P0.3a/P0.3b) are
caught automatically instead of only showing up as console noise in `just test` output.

**Issue(s):** [#31](https://github.com/PyXiion/PxModRim/issues/31).

**Depends on:** P0.3a and P0.3b — fix the known QML errors first so the new infrastructure
protects a clean baseline instead of being built around known-broken output.

**Acceptance criteria:**
- QML tests run in `ci.yml` on all three OS (`ubuntu-latest`, `windows-latest`, `macos-latest`).
- The suite fails (non-zero exit / red CI check), not just logs a warning, when a QML runtime
  error occurs during a test run — e.g. an `Invalid image provider` line or a
  `TypeError: Cannot read property … of null` line such as the ones this phase eliminated.

**Out of scope:** a general-purpose QML test-authoring framework beyond fail-on-runtime-error;
visual regression testing.

### P1.4 — [#49](https://github.com/PyXiion/PxModRim/issues/49) Config/DB migrations

**Goal:** `CHANGELOG.md`'s `[Unreleased]` section already lists a migration system
(`core/migrator.py` ordered-step migrator, a `Service` protocol with `setup()`, atomic config
saves via tmp-file + `os.replace`, and a pre-migration backup
`config.json.bak.{timestamp}`), so the remaining work is verifying and closing the remaining
scope, not building the mechanism from scratch.

**Issue(s):** [#49](https://github.com/PyXiion/PxModRim/issues/49).

**Depends on:** Phase P0 complete.

**Acceptance criteria:**
- A versioned schema chain exists and is documented.
- Automated tests cover: a fresh install (no prior config/DB), an upgrade from each prior schema
  version, a corrupted/unreadable schema file, and at least one fixture representing an old
  schema.
- No test or manual run demonstrates user-data loss across any of those paths.

**Out of scope:** designing new config fields or schema content unrelated to the migration
mechanism itself.

---

## Phase P2 — Product core UX (the things that make it usable daily)

### P2.1 — [#62](https://github.com/PyXiion/PxModRim/issues/62) Disable dependent mods when disabling a mod

**Goal:** when a mod is disabled, mods that depend on it should be handled consistently rather
than left active and broken. Labelled `good first issue`, milestone `0.7.0`.

**Issue(s):** [#62](https://github.com/PyXiion/PxModRim/issues/62).

**Depends on:** Phase P1 complete.

**Acceptance criteria:**
- Disabling a mod that other active mods depend on visibly disables those dependents (or prompts
  the user and disables on confirmation).
- A test exercises this against the existing dependency-graph service.

**Out of scope:** building a full conflict-resolution UI — that is P2.2 / [#26](https://github.com/PyXiion/PxModRim/issues/26).

### P2.2 — [#26](https://github.com/PyXiion/PxModRim/issues/26) Mod conflict resolution UI (dependency graph)

**Goal:** surface mod dependency/load-order conflicts through a dedicated UI instead of only
inline diagnostics.

**Issue(s):** [#26](https://github.com/PyXiion/PxModRim/issues/26).

**Depends on:** Phase P1 complete; benefits from but does not strictly require P2.1.

**Acceptance criteria:**
- A UI view exists that shows conflicting/circular dependencies for the current mod list and lets
  the user act on them.
- Verified via a UI test or a documented manual walkthrough — not just a data-layer change with no
  observable UI.

**Out of scope:** automatic conflict auto-resolution beyond what the existing sorter already does.

### P2.3 — [#23](https://github.com/PyXiion/PxModRim/issues/23) Undo/Redo + [#24](https://github.com/PyXiion/PxModRim/issues/24) Mod list snapshot and rollback

**Goal:** both issues revert mod-list state; sequence them on one shared change-model instead of
building two independent history mechanisms.

**Issue(s):** [#23](https://github.com/PyXiion/PxModRim/issues/23),
[#24](https://github.com/PyXiion/PxModRim/issues/24).

**Depends on:** #23 first establishes the shared change-model (a recorded sequence of mod-list
mutations); #24 then reuses that same model for named snapshots/rollback rather than building a
parallel snapshot store.

**Acceptance criteria:**
- A single undo/redo stack (or equivalent event-sourced model) backs both keyboard-driven
  undo/redo (#23) and named snapshot/rollback (#24).
- There is no second, independent history implementation between the two features.

**Out of scope:** undo/redo for application settings/preferences outside the mod list itself.

### P2.4 — [#7](https://github.com/PyXiion/PxModRim/issues/7) Presets Service

**Goal:** allow saving/loading named presets of mod list plus configuration.

**Issue(s):** [#7](https://github.com/PyXiion/PxModRim/issues/7).

**Depends on:** P1.4 (migrations settled) — presets persist to disk/DB and should not be built
against a config/DB layer whose schema-evolution story is still open.

**Acceptance criteria:**
- A preset can be saved, listed, and reloaded, restoring the mod list/config it captured.
- A round-trip test exists: save preset → mutate state → load preset → state matches the saved
  snapshot.

**Out of scope:** cloud sync or sharing presets between users.

### P2.5 — [#34](https://github.com/PyXiion/PxModRim/issues/34) Structured file logging + [#15](https://github.com/PyXiion/PxModRim/issues/15) More useful logs

**Goal:** get logging content and format right before building a log viewer on top of it
([#8](https://github.com/PyXiion/PxModRim/issues/8), Phase P4) — logging is a prerequisite of the
viewer, not the other way around.

**Issue(s):** [#34](https://github.com/PyXiion/PxModRim/issues/34),
[#15](https://github.com/PyXiion/PxModRim/issues/15).

**Depends on:** Phase P1 complete.

**Acceptance criteria:**
- Application logs are written to a file in structured, parseable form (#34).
- Log messages cover the situations #15 identifies as currently unhelpful — verify by reproducing
  one such situation and grepping the resulting log file for a useful message.

**Out of scope:** building the log viewer UI itself — that is [#8](https://github.com/PyXiion/PxModRim/issues/8) in Phase P4.

---

## Phase P3 — Release & distribution

### P3.1 — [#36](https://github.com/PyXiion/PxModRim/issues/36) Packaging: Flatpak, .deb, .rpm, Windows MSI, macOS .app

**Goal:** produce installable artifacts so PxModRim can be run without cloning the source tree.

**Issue(s):** [#36](https://github.com/PyXiion/PxModRim/issues/36).

**Depends on:** Phase P0 and P1 — do not package a build with known QML errors or an unverified
test suite.

**Proposed staged order** (turns one large issue into a sequence of shippable milestones):
1. Linux artifact — `build.yml`'s `build-linux` job already produces this; harden and verify it.
2. Windows artifact — `build.yml`'s `build-windows` job already produces this; harden and verify
   it.
3. macOS `.app` — not yet in `build.yml` (today it only has `build-linux` and `build-windows`
   jobs, plus a `release` job that needs both).
4. Flatpak / `.deb` / `.rpm` / Windows MSI as later, lower-priority formats once the three base OS
   artifacts are solid.

**Acceptance criteria:**
- At minimum, the stage-1 (Linux) artifact runs on a clean machine with no PxModRim source tree
  present.
- The artifact includes the QML files, SVG assets, the built `inject.js`, and plugin assets.
- `build.yml`, triggered by a `v*` tag push, produces that artifact without manual steps.

**Out of scope:** store-specific packaging metadata/signing (macOS notarization, MSI code
signing) — track as a follow-up once the base artifact works. `build.yml` currently runs no
tests; adding a test gate to the release build is also out of scope here (Phase P0/P1 already
gate this on the `ci.yml` side).

### P3.2 — Update README installation section

**Goal:** `README.md`'s "Installation" section currently states "No packaged installer yet." That
needs to change once an artifact exists.

**Issue(s):** No issue filed yet — needs filing (or close alongside [#36](https://github.com/PyXiion/PxModRim/issues/36)).

**Depends on:** P3.1 stage 1 (Linux artifact) landing, at minimum.

**Acceptance criteria:**
- The "Installation" section in `README.md` documents how to get and run the packaged artifact,
  not only the from-source developer path.

**Out of scope:** a full user-facing documentation site — this is a README section update only.

---

## Phase P4 — Convenience and later

None of the items below block a "ready for use" release; they are explicitly sequenced after
Phases P0–P3 and are independent of each other unless a dependency is called out.

- [#35](https://github.com/PyXiion/PxModRim/issues/35) Extended keyboard shortcuts — `good first
  issue`.
- [#33](https://github.com/PyXiion/PxModRim/issues/33) Accessibility for QML components —
  currently milestone `Cool features`.
- [#8](https://github.com/PyXiion/PxModRim/issues/8) Log Viewer — depends on P2.5 (#34/#15
  logging work) landing first; do not build a viewer against a log format that is still changing.
- [#9](https://github.com/PyXiion/PxModRim/issues/9) Translation +
  [#10](https://github.com/PyXiion/PxModRim/issues/10) Translation/Russian — sequence together
  (shared i18n infrastructure); #9 also carries `help wanted`/`good first issue` labels.
- [#25](https://github.com/PyXiion/PxModRim/issues/25) Drag-and-drop mod installation from
  filesystem.
- [#37](https://github.com/PyXiion/PxModRim/issues/37) Mod Organizer Plugin — Tree view for
  browsing & toggling mods.
- [#29](https://github.com/PyXiion/PxModRim/issues/29) Mod metadata cache — note that
  [#39](https://github.com/PyXiion/PxModRim/issues/39) (the original "startup with 1341 mods
  takes 0.8s, too slow" issue) is already **closed**, so this needs a fresh profiling baseline
  before implementation, not a re-read of the old numbers. No profiling-baseline issue filed yet —
  file one if a fresh baseline surfaces a real bottleneck.
- [#11](https://github.com/PyXiion/PxModRim/issues/11) CLI (Headless mode) — currently milestone
  `Cool features`.
- [#27](https://github.com/PyXiion/PxModRim/issues/27) Self-update mechanism — depends on P3.1
  (packaging); a self-updater needs something packaged to update.
- [#17](https://github.com/PyXiion/PxModRim/issues/17) AOT mod optimisation & Built-in Companion
  Mod for Fast Loading — highest risk (touches a separate C# companion-mod submodule and Harmony
  patching), lowest obligation (`help wanted`, milestone `Cool features`); treat as opportunistic,
  not scheduled.

**Out of scope for all of Phase P4:** letting any of these design conversations block Phase
P0–P3 work; none of them gate `0.7.0`.

---

## Maintenance lane (runs in parallel, does not block feature phases)

All 7 open pull requests are Dependabot dependency bumps. After refreshing refs read-only,
`origin/main` and local `main` diverge (`git rev-list --left-right --count origin/main...HEAD`
reports `7 6`): origin has seven commits absent locally, while local `HEAD` has six commits not
yet upstream. Origin includes `75c0f167`, which bumps Ruff `0.15.22 → 0.16.0`; local `HEAD` still
locks `0.15.22`. The historical PR #98 run (`35575295266`) used `headSha=f4bd9de...`, whose
`uv.lock` also pins `0.16.0`, and its 45-error summary is reproducible with Ruff `0.16.0`.
`Codacy Security Scan` fails separately on all 7 PRs and is a distinct, already-diagnosed
incident (SARIF upload rejected: `22 > 20` runs — see Phase P0.5); it does not gate merging
these dependency bumps. `Codacy Static Code Analysis` succeeds and the 22 Codacy reporter checks
are neutral on all of them.

| PR | Bump | CI |
|---:|---|---|
| [#98](https://github.com/PyXiion/PxModRim/pull/98) | `taiki-e/install-action` 2.85.11 → 2.87.15 (GitHub Actions) | red |
| [#97](https://github.com/PyXiion/PxModRim/pull/97) | `github/codeql-action` 4.37.6 → 4.38.1 (GitHub Actions) | red |
| [#95](https://github.com/PyXiion/PxModRim/pull/95) | `pyright` 1.1.411 → 1.1.414 (dev tooling) | red |
| [#94](https://github.com/PyXiion/PxModRim/pull/94) | `ruff` 0.16.0 → 0.16.7 (dev tooling) | red |
| [#90](https://github.com/PyXiion/PxModRim/pull/90) | `pydeps` 3.0.7 → 3.0.8 (dev tooling) | red |
| [#87](https://github.com/PyXiion/PxModRim/pull/87) | `nuitka` 4.1.3 → 4.2.1 (packaging/build) | red |
| [#84](https://github.com/PyXiion/PxModRim/pull/84) | `lxml` 6.1.1 → 6.1.3 (runtime dependency) | red |

"Red" above reflects each PR's last-checked (stale) commit, not current `main` — refresh (Step 0
below) before judging any of them.

**Open question (answered after refreshing refs):** this was a stale remote-tracking ref, not a
local Ruff downgrade. `git rev-list --left-right --count origin/main...HEAD` now reports `7 6`;
the seven upstream-only commits include `75c0f167 chore(deps-dev): bump ruff from 0.15.22 to
0.16.0`. `origin/main:uv.lock` pins `0.16.0`, local `HEAD:uv.lock` pins `0.15.22`, and the
fetched `f4bd9de` commit (ancestor of `pr/98`, but neither `HEAD` nor `origin/main`) pins
`0.16.0`. Its `pyproject.toml` requires `ruff>=0.16.0`. Ruff `0.16.0` read-only over the current
widened scope reports `45 errors (24 fixable, 21 remaining)`, exactly matching the historical
summary. PR #94's head locks `0.16.7`; reconcile/push refs and make the Ruff-version decision
before refreshing.
**Step 0 (prerequisite, applies before anything below):** reconcile local `main`'s 6 commits with
the 7 upstream-only commits now present on `origin/main`, push the resulting `main`, then refresh
all 7 PRs onto it (`@dependabot rebase`, or close/reopen) and let CI re-run. Read each PR's own
result individually — do not assume a refresh fixes anything until its checks are inspected.

Recommended merge order, applied only to PRs that show green `ci` after the Step 0 refresh:

1. Actions bumps first — [#97](https://github.com/PyXiion/PxModRim/pull/97),
   [#98](https://github.com/PyXiion/PxModRim/pull/98) — lowest risk, CI-config-only.
2. Dev tooling — [#94](https://github.com/PyXiion/PxModRim/pull/94),
   [#95](https://github.com/PyXiion/PxModRim/pull/95),
   [#90](https://github.com/PyXiion/PxModRim/pull/90) — affects lint/type-check/dependency-graph
   output, easy to bisect individually.
3. Runtime dependency — [#84](https://github.com/PyXiion/PxModRim/pull/84) (`lxml` is used at
   runtime; worth its own smoke test beyond a green CI check).
4. [#87](https://github.com/PyXiion/PxModRim/pull/87) (`nuitka`) last — a Nuitka bump should get a
   packaging smoke test (build an artifact and run it), and packaging only stabilizes once Phase
   P3 lands.

**Rule:** do not merge on a stale-base red run. Refresh the PR first (Step 0), then read the
actual result — a red run against an old base says nothing about whether the bump itself is
safe. `Codacy Security Scan` staying red does not block merging these dependency bumps; it is
tracked and fixed independently in Phase P0.5, and `main`'s `ci` workflow is not the failing
workflow either way — the newest failing workflow on `main` is the scheduled Codacy scan.

**Open question (answered; same fetched result as P0.2):** PR #94's `ruff` `0.16.0 → 0.16.7`
bump is based on the Ruff-`0.16.0` line now visible in refreshed `origin/main`, not on a hidden
local downgrade. `f4bd9de` is present after fetching PR refs, is an ancestor of `pr/98` only,
and its `uv.lock` pins `0.16.0`; refreshed `origin/main` also pins `0.16.0`, while local `HEAD`
pins `0.15.22`. Running Ruff `0.16.0` read-only over the widened scope reproduces
`45 errors (24 fixable, 21 remaining)`, so the historical CI result corresponds to its
Ruff-0.16.0 base. PR #94's `0.16.7` lock is newer and needs the same deliberate version/fix
decision. Keep the reconciliation, push, and refresh actions below pending for the maintainer.
---

## Milestone rebaseline

Milestone `0.7.0` ("Make the manager ready for use") is due `2026-08-31`, 21 days in the past as
of `2026-09-21`, and currently holds 14 open issues spanning true release blockers (packaging,
coverage) down to speculative UX (translations, tree view, self-update). That scope cannot land as
a single overdue milestone.

Proposal: keep `0.7.0` scoped to "ready for use" — only Phase P0/P1 follow-through issues plus the
Phase P3 packaging deliverable — and move everything else to milestones that don't exist yet
(`0.8.0`, `0.9.0`) or leave it in the existing `Cool features` backlog.

| Issue | Current milestone | Proposed milestone | Rationale |
|---:|---|---|---|
| [#62](https://github.com/PyXiion/PxModRim/issues/62) Disable dependent mods | 0.7.0 | 0.7.0 | Small, `good first issue`, closes a real correctness gap before calling the app ready |
| [#49](https://github.com/PyXiion/PxModRim/issues/49) Config/DB migrations | — | 0.7.0 | Already mostly implemented per `CHANGELOG.md`; closing the verification scope protects user data before release |
| [#40](https://github.com/PyXiion/PxModRim/issues/40) Core coverage ≥60% | 0.7.0 | 0.7.0 | Quality gate for a release build |
| [#30](https://github.com/PyXiion/PxModRim/issues/30) Test coverage for core modules | 0.7.0 | 0.7.0 | Likely duplicate of #40 — same target, flag for maintainer to close one |
| [#31](https://github.com/PyXiion/PxModRim/issues/31) QML/UI testing infra | — | 0.7.0 | Needed to keep the Phase P0 QML fixes from regressing silently |
| [#36](https://github.com/PyXiion/PxModRim/issues/36) Packaging | 0.7.0 | 0.7.0 | "Ready for use" requires something installable; this is the actual release blocker |
| [#34](https://github.com/PyXiion/PxModRim/issues/34) Structured file logging | 0.7.0 | 0.7.0 | Needed to support users of a first public release |
| [#37](https://github.com/PyXiion/PxModRim/issues/37) Tree view | 0.7.0 | 0.8.0 | Product-core UX, not a release blocker |
| [#35](https://github.com/PyXiion/PxModRim/issues/35) Extended keyboard shortcuts | 0.7.0 | 0.8.0 | Convenience, not a release blocker |
| [#29](https://github.com/PyXiion/PxModRim/issues/29) Mod metadata cache | 0.7.0 | 0.8.0 | Needs a fresh profiling baseline first (see Phase P4); not a release blocker |
| [#25](https://github.com/PyXiion/PxModRim/issues/25) Drag-and-drop | 0.7.0 | 0.8.0 | Product-core UX, not a release blocker |
| [#15](https://github.com/PyXiion/PxModRim/issues/15) More useful logs | 0.7.0 | 0.8.0 | Content follow-up to #34, not itself a release blocker |
| [#7](https://github.com/PyXiion/PxModRim/issues/7) Presets Service | 0.7.0 | 0.8.0 | Product-core UX, depends on #49 settling first |
| [#26](https://github.com/PyXiion/PxModRim/issues/26) Mod conflict resolution UI | — | 0.8.0 | Product-core UX |
| [#24](https://github.com/PyXiion/PxModRim/issues/24) Snapshot/rollback | — | 0.8.0 | Product-core UX, shares a change-model with #23 |
| [#23](https://github.com/PyXiion/PxModRim/issues/23) Undo/Redo | — | 0.8.0 | Product-core UX, shares a change-model with #24 |
| [#27](https://github.com/PyXiion/PxModRim/issues/27) Self-update | 0.7.0 | 0.9.0 | Needs packaging (#36) to exist first |
| [#10](https://github.com/PyXiion/PxModRim/issues/10) Translation/Russian | 0.7.0 | 0.9.0 | Polish; needs shared i18n work with #9 |
| [#9](https://github.com/PyXiion/PxModRim/issues/9) Translation | 0.7.0 | 0.9.0 | Polish; needs shared i18n work with #10 |
| [#33](https://github.com/PyXiion/PxModRim/issues/33) Accessibility | Cool features | Cool features | No change — already correctly scoped as optional |
| [#17](https://github.com/PyXiion/PxModRim/issues/17) AOT/companion mod | Cool features | Cool features | No change — highest risk, lowest obligation |
| [#11](https://github.com/PyXiion/PxModRim/issues/11) CLI | Cool features | Cool features | No change — optional |
| [#8](https://github.com/PyXiion/PxModRim/issues/8) Log Viewer | Cool features | Cool features | No change — depends on #34/#15 landing first regardless of milestone |

`0.8.0` and `0.9.0` do not exist in the tracker yet; this table proposes creating them rather than
silently overloading `0.7.0` a second time. This also resolves the 5 currently unmilestoned open
issues — [#49](https://github.com/PyXiion/PxModRim/issues/49) → `0.7.0`,
[#31](https://github.com/PyXiion/PxModRim/issues/31) → `0.7.0`,
[#26](https://github.com/PyXiion/PxModRim/issues/26) → `0.8.0`,
[#24](https://github.com/PyXiion/PxModRim/issues/24) → `0.8.0`,
[#23](https://github.com/PyXiion/PxModRim/issues/23) → `0.8.0`.

**Proposed new `0.7.0` due date: `2026-11-13`** (about 8 weeks from today, `2026-09-21`).
Rationale: Phase P0 fixes are small (days); Phase P1 (coverage tooling and baseline, QML test
infrastructure, migration verification) is roughly 1–2 weeks of net-new engineering; the Phase P3
packaging deliverable is the long pole because `build.yml` today only produces Linux and Windows
artifacts, and the staged plan also brings macOS in before deferring Flatpak/`.deb`/`.rpm`/MSI
further out. Eight weeks gives that staged rollout room without repeating the mistake of the
original `2026-08-31` date, which packed in 14 issues of mixed priority and missed by three weeks.

---

## Definition of done for 0.7.0

- `just check` exits 0 (already true at current `HEAD` per P0.2 — confirm on a refreshed CI run,
  not just locally) and `just test` passes, both on `ubuntu-latest`, `windows-latest`, and
  `macos-latest`, across at least 5 consecutive CI runs on `main` — a single green run is not
  sufficient, since the suite has demonstrated non-determinism (`1 failed, 197 passed` vs.
  `198 passed` on unmodified code).
- CI verifies both lint (`ruff check`, no `--fix`) and formatting (`ruff format --check`)
  non-mutatingly for the paths CI covers, so a violation is caught instead of being silently
  fixed-and-discarded or missed entirely (per P0.4).
- No test performs real network I/O (the SteamCMD download path is mocked/monkeypatched), per
  P0.1.
- `just test` output contains zero `Invalid image provider` lines and zero
  `TypeError: Cannot read property … of null` lines.
- `src/pxmodrim/core` coverage is measured by `just coverage` (or equivalent) and reported at
  ≥60%, enforced in CI.
- A fresh-install run (no prior config/DB) starts cleanly; an upgrade run from a fixture of an
  older schema completes without data loss.
- The packaged artifact from Phase P3 (at minimum, Linux) launches and runs on a machine with no
  PxModRim source tree present.
- A `ModsConfig.xml` round-trip (load an existing file, make no changes, save, diff against the
  original) produces no unintended changes.
- All 7 open Dependabot PRs are either merged (per the Maintenance lane order) or explicitly
  triaged with a documented reason for staying open.

---

## Deliberate scope notes

- This document proposes changes only; it does not fix code, file issues, or edit milestones.
- Every issue number, label, milestone, file path, and line number above is taken from the
  repository and tracker snapshot recorded on 2026-09-21; nothing here is invented.
