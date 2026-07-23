## Why

There is no separate release branch for the resin/FDM mixed build — the main (FDM) line keeps advancing, and engineers manually build the mixed variant off the current main-line checkout using the new `PHROZEN_ORCA_ENABLE_RESIN` flag (introduced in `phrozen-disable-resin-update-check`). Today that flag only gates the app-update check. The broader goal is: when building the **main (FDM) version** (flag `OFF`), none of the resin/SLA-specific functionality, parameters, or UI added by the ongoing PrusaSlicer-SLA-merge project should appear or be usable, to avoid accidental misuse by FDM-only users/printers.

## What Changes

**Not yet defined — this is a placeholder pending a dedicated exploration/analysis pass.** Expected shape of the eventual work, based on discussion so far:

- Audit everything the SLA-merge project has added to date (Phase 1 items: PrintConfig SLA parameters, ZCorrection, Archive/rasterization, unified SLA tabs, SL1 profiles, sla-support-weight, island-detection, CSGMesh architecture, and any resin-specific printer presets/vendor profiles such as `PhrozenSLA`).
- Decide, per area, whether to gate with `#ifdef PHROZEN_ORCA_ENABLE_RESIN` (compile-time exclusion) or a runtime UI-hiding approach, and the trade-offs between them (binary size / maintenance burden vs. risk of a hidden-but-still-reachable code path).
- Decide the fate of the `phrozen_work_mode` resin/filament toolbar-mode switch (`MainFrame.cpp`) in the main build — likely removed or hard-locked to "filament" when the flag is off.

## Capabilities

### New Capabilities
- TBD — to be filled in once the audit above is done. Likely candidates: `resin-feature-gating` (parameters/UI), `resin-printer-profile-gating` (vendor/preset visibility).

### Modified Capabilities
- TBD — depends on audit; will likely touch existing SLA-related specs under `openspec/specs/sla-*` and `openspec/specs/prz-*` if any of their requirements become conditional on the build variant.

## Impact

Likely broad: touches `PrintConfig.hpp/.cpp` (SLA parameter registration), SLA tab/UI code, printer preset/vendor directory logic (`MainFrame.cpp`), and potentially the CSGMesh/support pipeline additions from prior changes. Scope and risk should be sized during the dedicated exploration pass before committing to a design or task breakdown — do not start implementation from this placeholder alone.
