# Spec: resin-build-update-check-gate

## Purpose

Define how the app-version update-check (startup auto-check, manual Help-menu check, and force-upgrade prompt) is gated for the resin/FDM mixed build variant of PhrozenOrca, so that the mixed build never mistakes a main (FDM) line release for its own newer version, while the main build's update-check behavior remains completely unchanged.

## Requirements

### Requirement: Build-variant flag for resin/FDM mixed build
The build system SHALL provide a CMake option `PHROZEN_ORCA_ENABLE_RESIN`, defaulting to `OFF`, independent of and uncoupled from the existing `BUILD_PHROZEN_ORCA` flag. When enabled, it SHALL propagate to the C++ compilation as a preprocessor definition usable via `#ifdef PHROZEN_ORCA_ENABLE_RESIN`.

#### Scenario: Default build produces main (FDM) behavior
- **WHEN** the project is configured with CMake without explicitly setting `PHROZEN_ORCA_ENABLE_RESIN`
- **THEN** the option resolves to `OFF` and the resulting binary behaves identically to the main (FDM) build's update-check behavior

#### Scenario: Resin build script enables the flag
- **WHEN** `build_resin_release_vs2022.bat` configures the project
- **THEN** it passes `-DPHROZEN_ORCA_ENABLE_RESIN=ON` and the resulting binary is compiled with `PHROZEN_ORCA_ENABLE_RESIN` defined

#### Scenario: Existing PhrozenOrca-fork identity flag is unaffected
- **WHEN** `PHROZEN_ORCA_ENABLE_RESIN` is set to either `ON` or `OFF`
- **THEN** `BUILD_PHROZEN_ORCA` and its `PhrozenConnect`/`PrusaLink` print-host branching remain unchanged and unaffected by this flag

### Requirement: Suppressed app-update-check in the resin/FDM mixed build
When compiled with `PHROZEN_ORCA_ENABLE_RESIN` enabled, the application SHALL NOT perform or surface any app-version update-check UI: no automatic startup check, no manual Help-menu check entry, and no force-upgrade prompt. When `PHROZEN_ORCA_ENABLE_RESIN` is not enabled (main build), update-check behavior SHALL be unchanged from current behavior.

#### Scenario: Startup auto-check is skipped in the resin build
- **WHEN** the resin/FDM mixed build (`PHROZEN_ORCA_ENABLE_RESIN` enabled) starts up
- **THEN** it SHALL NOT call `GUI_App::check_new_version_sf()` from the startup `CallAfter` block, and SHALL NOT show the `UpdateVersionDialog`

#### Scenario: Help menu has no "Check for Update" item in the resin build
- **WHEN** the resin/FDM mixed build's Help menu is constructed
- **THEN** the "Check for Update" menu item SHALL NOT be added

#### Scenario: Force-upgrade prompt cannot fire in the resin build
- **WHEN** `GUI_App::check_update()` runs in the resin/FDM mixed build (e.g. if reached through any residual code path)
- **THEN** the `force_upgrade` branch SHALL be skipped and `enter_force_upgrade()` SHALL NOT be invoked

#### Scenario: Main build update-check is unchanged
- **WHEN** the main (FDM) build (`PHROZEN_ORCA_ENABLE_RESIN` at its default `OFF`) starts up or the user clicks "Check for Update"
- **THEN** it behaves exactly as before this change: startup auto-check runs, the Help-menu item is present, and the force-upgrade branch executes when the server response requests it

### Requirement: Unrelated update mechanisms remain untouched
The preset/profile update mechanism (`GUI_App::check_updates()`, backed by `AppConfig::profile_update_url()`) SHALL be unaffected by `PHROZEN_ORCA_ENABLE_RESIN` in either build variant.

#### Scenario: Profile update check still runs in the resin build
- **WHEN** the resin/FDM mixed build starts up
- **THEN** `PresetUpdater`-driven profile/config update checks SHALL continue to run exactly as in the main build
