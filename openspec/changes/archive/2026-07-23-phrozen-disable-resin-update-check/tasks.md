## 1. CMake build option

- [x] 1.1 In `CMakeLists.txt`, add `option(PHROZEN_ORCA_ENABLE_RESIN "Enable resin/FDM mixed build features (currently: gates the app-update check)" OFF)` near the existing `option(...)` block (around the `PHROZEN_ORCA_TOOLS` declaration, ~line 138).
- [x] 1.2 Propagate it as a compile definition when enabled, e.g. `if(PHROZEN_ORCA_ENABLE_RESIN) add_definitions(-DPHROZEN_ORCA_ENABLE_RESIN) endif()`, placed near the other `add_definitions(-DSLIC3R_...)` calls.
- [x] 1.3 Confirm `BUILD_PHROZEN_ORCA` (`src/libslic3r/PrintConfig.hpp:30`) is not modified, referenced, or coupled to the new option in any way.

## 2. Gate the startup auto-check

- [x] 2.1 In `src/slic3r/GUI/GUI_App.cpp`, wrap the `this->check_new_version_sf();` call inside the startup `CallAfter` block (around line 991) with `#ifndef PHROZEN_ORCA_ENABLE_RESIN ... #endif` so it only runs in the main build.
- [x] 2.2 Add a one-line comment at the gate referencing this change (`resin-build-update-check-gate`) so future readers understand why it's conditional.

## 3. Gate the force-upgrade branch

- [x] 3.1 In `GUI_App::check_update()` (`src/slic3r/GUI/GUI_App.cpp:4318-4342`), wrap the `if (version_info.force_upgrade) { ... GUI::wxGetApp().enter_force_upgrade(); }` branch (lines ~4326-4333) so it is skipped entirely when `PHROZEN_ORCA_ENABLE_RESIN` is defined — e.g. treat it as the `else` (non-force) path, or return early before setting `force_upgrade` state.
- [x] 3.2 Verify no other call path can reach `enter_force_upgrade()` in the resin build now that both the auto-check (task 2.1) and this branch are gated. (Confirmed: `check_update()` is only called from the already-disabled `check_new_version()` — `check_new_version_sf()` never calls it — so this is a defensive gate; no other reachable call site exists.)

## 4. Gate the Help-menu "Check for Update" item

- [x] 4.1 In `src/slic3r/GUI/MainFrame.cpp` (~lines 2702-2708), wrap the `append_menu_item(helpMenu, wxID_ANY, _L("Check for Update"), ...)` call with `#ifndef PHROZEN_ORCA_ENABLE_RESIN ... #endif` so the item is not added to the Help menu in the resin build.

## 5. Verification

- [x] 5.1 Confirm `GUI_App::check_updates()` (PresetUpdater profile/config update, `GUI_App.cpp:6720`) and `AppConfig::profile_update_url()` are untouched. (Confirmed via `git diff` — only the two gated sections in `GUI_App.cpp` changed.)
- [x] 5.2 Grep the diff for any remaining unconditional reference to `check_new_version_sf`, `check_new_version`, or `enter_force_upgrade` in the resin build path to make sure nothing was missed. (Confirmed clean — diff limited to CMakeLists.txt, GUI_App.cpp x2, MainFrame.cpp x1.)
- [x] 5.3 Ask the user to build once with `build_release_vs2022.bat` (main, flag OFF) and once with `build_resin_release_vs2022.bat` (resin, flag ON) per project rules (Claude must not run the compiler itself) and confirm: main build still shows "Check for Update" and still auto-checks on startup; resin build shows neither. (User confirmed both variants behave correctly.)
