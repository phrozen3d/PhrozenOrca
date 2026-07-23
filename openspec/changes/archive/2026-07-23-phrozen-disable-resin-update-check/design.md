## Context

This repo is a resin/FDM mixed special build of PhrozenOrca. The app-update check (`GUI_App::check_new_version_sf()`, `src/slic3r/GUI/GUI_App.cpp:4420-4579`) queries a hardcoded GitHub releases URL (`src/libslic3r/AppConfig.cpp:40-41`) that belongs to PhrozenOrca's main (FDM) release channel. There is currently no way to build a variant of this codebase that behaves differently — no CMake option, no compile-time flag, nothing beyond the always-on `#define BUILD_PHROZEN_ORCA 1` in `PrintConfig.hpp:30`.

That existing `BUILD_PHROZEN_ORCA` flag is not a candidate for reuse: it means "this is a PhrozenOrca fork, not upstream OrcaSlicer" and gates the `PhrozenConnect` vs `PrusaLink` print-host branch (`PhysicalPrinterDialog.cpp:3992, 148, 464`). Project rules explicitly forbid modifying `BUILD_PHROZEN_ORCA` or `PhrozenConnect`-related code. It is also semantically orthogonal to "is this the resin/FDM mixed build" — repurposing it would risk disabling PhrozenConnect in the main build by accident.

There is no separate long-lived branch for the resin/FDM mixed build. The main (FDM) line keeps advancing, and engineers manually build the mixed variant off the current main-line checkout by invoking a different build script with a different CMake flag. This means the distinguishing mechanism must be a build-time flag on a single shared codebase, not a branch-based split.

## Goals / Non-Goals

**Goals:**
- Add a new, independent CMake option `PHROZEN_ORCA_ENABLE_RESIN` that distinguishes the resin/FDM mixed build from the main build at compile time.
- When that option is enabled, suppress all app-update-check UI surfaces: startup auto-check, manual Help-menu check, and the force-upgrade prompt.
- Leave the main build's update-check behavior byte-for-byte unchanged (option defaults to `OFF`).
- Keep `BUILD_PHROZEN_ORCA` completely untouched — zero risk of merge conflict or semantic confusion when this branch is eventually merged back into the main line.

**Non-Goals:**
- Deciding whether/how resin-specific parameters, printer profiles, or UI panels should be hidden in the main build. That is a much larger, separate future change.
- Giving the resin/FDM mixed build its own update-check channel (e.g., a separate GitHub repo/release feed). Not needed now; `AppConfig::version_check_url()` already supports an ini override (`version_check_url` key) if this is ever wanted later.
- Changing `PresetUpdater`'s profile/config update flow (`GUI_App::check_updates()`, `AppConfig::profile_update_url()`) — unrelated mechanism, out of scope.

## Decisions

**D1. New flag name: `PHROZEN_ORCA_ENABLE_RESIN`, default `OFF`.**
Follows the existing `option(...)` naming convention in `CMakeLists.txt` (e.g. `PHROZEN_ORCA_TOOLS`). Default `OFF` is the fail-safe choice: since there is no separate branch and the mixed build is produced by manually passing an extra `-D` flag, forgetting the flag must fall back to main-build behavior (no resin exposure), never the reverse. This mirrors `add_definitions(-DSLIC3R_PROFILE)`-style propagation already used in the same file, so `#ifdef PHROZEN_ORCA_ENABLE_RESIN` reads consistently with existing conditional code in this codebase.

Alternative considered: reusing/renaming `BUILD_PHROZEN_ORCA`. Rejected — see Context above; different axis, explicitly protected by project rules.

Alternative considered: runtime-only toggle via `AppConfig` (ini setting), no CMake option. Rejected as the sole mechanism — an ini value can be edited by any user, so it cannot guarantee the main build never exposes resin-only code paths added by later, larger changes. A compile-time flag remains the foundation; a future runtime override (e.g., for support/debugging) can still be layered on top of it later if needed.

**D2. Scope of this change: only the three update-check surfaces.**
`PHROZEN_ORCA_ENABLE_RESIN` is introduced now but only consumed at three call sites (see tasks.md). No resin-specific parameter/UI gating is attempted in this change — that requires a full audit of the SLA-merge work to date and is deferred to a separate, larger change.

**D3. Force-upgrade branch is in-scope, not just the dialog.**
`check_update()`'s `force_upgrade` branch (`GUI_App.cpp:4326-4333`) sets `app_config` flags and calls `enter_force_upgrade()`, which can put up a blocking dialog. Since this path is reached only through `check_new_version_sf()` → `check_update()`, gating the startup auto-call already prevents it from firing automatically; it is additionally hard-gated in `check_update()` itself so that even the manual Help-menu path (also removed in this change, but defensively) can never trigger a forced-upgrade prompt sourced from the main build's release feed.

**D4. `check_updates()` (PresetUpdater) is explicitly out of scope.**
It updates printer/filament/process profiles against `PROFILE_UPDATE_URL` (OrcaSlicer's profiles repo), not the app binary version, and is unrelated to the phrozen3d/PhrozenOrca release-based false positive this change addresses.

## Risks / Trade-offs

- [Risk] A future contributor adds a new update-check entry point without knowing about `PHROZEN_ORCA_ENABLE_RESIN`. → Mitigation: keep the flag check colocated with the existing three call sites and reference this change's spec (`resin-build-update-check-gate`) in code comments at each gate.
- [Risk] Build scripts diverge silently (someone edits `build_release_vs2022.bat` and forgets `build_resin_release_vs2022.bat` needs the same base changes). → Mitigation: `build_resin_release_vs2022.bat` mirrors the base script line-for-line except for the flag, install prefix, and build directory names; future base-script changes should be diffed against it manually until/unless a shared template is introduced.
- [Risk] Someone builds the resin variant into the same `build`/`PhrozenOrca` output directories as the main build, causing stale CMake cache or overwritten binaries. → Mitigation: `build_resin_release_vs2022.bat` already uses separate `build-resin*` directories and a separate `./PhrozenOrcaResin` install prefix.

## Migration Plan

Not applicable — this is a build-time flag addition with no data migration. Rollback is a plain revert of the CMakeLists.txt option, the three gated call sites, and the design/spec docs; `build_resin_release_vs2022.bat` can stay or be removed independently.

## Open Questions

- None blocking. Whether to eventually give the resin/FDM mixed build its own release channel/update URL is left for a future change if the need arises.
