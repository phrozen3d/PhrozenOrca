## Why

When the user manually places SLA support points and clicks the **Apply** button inside the Manual editing session, the resulting undo snapshot is recorded in the temporary *gizmo undo stack* rather than the main plater undo stack. That gizmo stack is discarded the moment the editing session ends (switching gizmo, pressing Escape, etc.), so the Apply is invisible to the main undo history. One Ctrl+Z then jumps all the way back to the state that existed *before* the SlaSupports gizmo was opened, clearing every support point the user just applied.

This is a latent bug first observed in `fix-sla-gizmo-undo-redo-crash` validation (archived item **[B]**) and never addressed.

## What Changes

- **Fix snapshot stack for Manual Apply button**: Before `TakeSnapshot("Support points edit")` is called inside the Manual-mode Apply handler, exit the gizmo undo stack so the snapshot lands in the main plater stack — matching the behaviour of `editing_mode_apply_changes()`.
- **Re-establish gizmo editing baseline after Apply**: After the main-stack snapshot is taken, re-enter the gizmo stack with a fresh "Gizmos-Initial" so subsequent in-session edits still have a safe local undo scope.
- **Guard double leave/enter of gizmos stack**: Verify that calling `leave_gizmos_stack()` / `enter_gizmos_stack()` mid-session is safe and does not corrupt the `m_undo_redo_stack_active` pointer.
- **Sync display cache in core Manual Apply restore flow**: In the undo/redo restore flow following a Manual Apply + gizmo switch, refresh the SlaSupports display cache from `mo->sla_support_points` so point spheres correctly reflect the restored state. Active SlaSupports undo routing is outside this change's scope.
- **Guard reslice / auto-generation overwrite**: Confirm that `reslice_until_step(slaposSupportPoints)` triggered by undo does not overwrite `UserModified` support points via the auto-generation path.

## Undo/Redo Entry Points

The undo/redo behaviour described in this change applies to **all user-facing undo/redo entry points**: the keyboard shortcuts Ctrl+Z / Ctrl+Y and the undo/redo toolbar buttons in the UI. Both entry points route through the same `Plater::priv::undo()` / `redo()` path and must produce consistent results after a Manual Apply.

Crash-safety across these entry points is an **acceptance constraint** for this change — not its primary subject. The Manual Apply scenario exposed an iterator-boundary crash in `Plater::priv::undo()` (as observed during testing); the required fix is the minimum boundary-safe iteration change needed to make the Manual Apply scenario safe, not a broad rewrite of the undo/redo architecture.

## Capabilities

### New Capabilities

- `sla-supports-apply-undo-stack`: Requirements for how a Manual SlaSupports Apply is recorded in the main undo stack, how the gizmo-stack baseline is reset after Apply, and how the display cache stays consistent with model data after undo/redo.

### Modified Capabilities

<!-- No existing spec covers undo-stack semantics for SlaSupports Apply. -->

## Impact

- **Primary**: `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — Manual Apply button handler (~line 1270), `editing_mode_apply_changes()` reference path (~line 1556), `data_changed()` undo-reload path (~line 115).
- **Secondary**: `src/slic3r/GUI/Gizmos/GLGizmosManager.cpp` — `update_after_undo_redo()` (~line 1109), which controls what happens to the active gizmo after undo.
- **Plater machinery**: `src/slic3r/GUI/Plater.cpp` — `enter_gizmos_stack()` / `leave_gizmos_stack()` (~lines 9060–9083) require safety verification for mid-session calls; `Plater::priv::undo()` requires a minimum boundary-safe iteration change (full boundary-safe loop) to prevent UB when the gizmo stack holds only the baseline snapshot (crash-safety constraint scoped to this scenario).
- No public API, no file-format, no profile changes.