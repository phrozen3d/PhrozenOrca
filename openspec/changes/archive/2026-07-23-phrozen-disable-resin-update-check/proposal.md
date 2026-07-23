## Why

The current working tree is a resin/FDM mixed special build of PhrozenOrca (`Phrozen_VERSION = "1.2.0-Education"`). Its app-update check is hardcoded to query `https://api.github.com/repos/phrozen3d/PhrozenOrca/releases` — the release feed of PhrozenOrca's **main (FDM) version**. Any new main-line release therefore looks like a "newer version" to this mixed build, incorrectly popping the update dialog (and potentially the force-upgrade dialog) and pointing users at the wrong installer. This must be suppressed specifically for the resin/FDM mixed build, without touching the main build's behavior.

## What Changes

- Introduce a new, independent CMake build option `PHROZEN_ORCA_ENABLE_RESIN` (default `OFF`), propagated as a compile definition, so the codebase can distinguish "resin/FDM mixed build" from "main (FDM) build" at compile time.
- When `PHROZEN_ORCA_ENABLE_RESIN` is enabled, skip the app-update-check entirely:
  - Skip the automatic startup call to `check_new_version_sf()`.
  - Do not register the "Check for Update" Help-menu item.
  - Skip the `force_upgrade` branch in `check_update()` so a main-line forced-upgrade flag can never block the resin build.
- Does **not** rename, redefine, or otherwise touch the existing `BUILD_PHROZEN_ORCA` flag or any `PhrozenConnect`-related branch — that flag identifies "PhrozenOrca fork vs. upstream OrcaSlicer" and is explicitly protected by project rules; it is orthogonal to the resin/FDM variant distinction.
- Does not change `GUI_App::check_updates()` (PresetUpdater profile/config update path) — that is a separate mechanism pointed at the OrcaSlicer profiles repo and is unaffected by this issue.
- Does not change which resin-specific parameters/UI are visible in the main build — that is future, separate work (a larger, deferred change).

## Capabilities

### New Capabilities
- `resin-build-update-check-gate`: Defines that when the app is built with the resin/FDM-mixed build variant (`PHROZEN_ORCA_ENABLE_RESIN`), the app-version update-check (startup auto-check, manual Help-menu check, and force-upgrade prompt) must not run or surface any UI, while the main (FDM) build's update-check behavior remains completely unchanged.

### Modified Capabilities
(none — no existing spec covers app-update-check or build-variant flags today)

## Impact

- `CMakeLists.txt`: add `option(PHROZEN_ORCA_ENABLE_RESIN ...)` and propagate as a compile definition.
- `src/slic3r/GUI/GUI_App.cpp`: gate the startup auto-check call and the `force_upgrade` branch of `check_update()`.
- `src/slic3r/GUI/MainFrame.cpp`: gate registration of the Help menu's "Check for Update" item.
- No changes to `AppConfig.cpp` URLs, `PresetUpdater`, or any resin-specific parameter/UI visibility.
- `build_resin_release_vs2022.bat` (already created outside this change's task list) is the build entry point that passes `-DPHROZEN_ORCA_ENABLE_RESIN=ON`; the existing `build_release_vs2022.bat` is untouched and continues to produce the main build with the flag left at its default `OFF`.
