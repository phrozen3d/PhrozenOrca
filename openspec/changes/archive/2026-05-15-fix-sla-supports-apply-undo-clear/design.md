## Context

`GLGizmoSlaSupports` uses two separate undo/redo stacks managed by `Plater::priv`:

- **Main stack** (`m_undo_redo_stack_main`): the global plater undo history visible to Ctrl+Z.
- **Gizmo stack** (`m_undo_redo_stack_gizmos`): a temporary per-session stack active while `m_editing_mode == true`. Created by `enter_gizmos_stack()` (saves a "Gizmos-Initial" snapshot), destroyed by `leave_gizmos_stack()` (calls `clear()` on the entire stack).

When the user enters Manual editing mode (`switch_to_editing_mode()`), the active stack switches to the gizmo stack. Any `TakeSnapshot` call while `m_editing_mode == true` therefore records into the gizmo stack.

The **Manual Apply button** (`GLGizmoSlaSupports.cpp:1270`) calls `TakeSnapshot("Support points edit")` while `m_editing_mode` is still `true`, so the snapshot lands in the gizmo stack. When the gizmo deactivates (`on_set_state Off` → `disable_editing_mode()` → `leave_gizmos_stack()`), the gizmo stack is cleared and the snapshot is lost. One subsequent Ctrl+Z undoes to the "Gizmos-Initial" snapshot on the main stack, restoring `mo->sla_support_points` to the state *before the editing session began* — clearing all applied supports.

By contrast, `editing_mode_apply_changes()` (triggered by Enter or implicit deactivation with unsaved changes) calls `disable_editing_mode()` **before** `TakeSnapshot`, so the snapshot correctly lands in the main stack.

There is also a secondary concern: when undo fires while SlaSupports is the current active gizmo, `update_after_undo_redo()` calls `data_changed()`, but `data_changed()` only invokes `reload_cache()` when the model-object ID changes. If the same object is active, `m_normal_cache` is never refreshed from the restored `mo->sla_support_points`, causing the rendered points to diverge from model state.

## Goals / Non-Goals

**Goals:**

- `TakeSnapshot("Support points edit")` triggered by the Manual Apply button SHALL land in the main undo stack, not the gizmo stack.
- After Manual Apply, the gizmo editing session SHALL continue with a fresh gizmo-stack baseline (so subsequent in-session Add/Delete/Move still have a local undo scope).
- After undo following a Manual Apply + gizmo switch, the visible support points SHALL reflect the pre-apply model state (not the pre-session state).
- In the core Manual Apply restore flow (undo/redo after leaving SlaSupports), the display cache (`m_normal_cache`) SHALL be refreshed so point spheres reflect the restored `mo->sla_support_points`. Active SlaSupports undo routing is out of scope (see [KB-4]).
- Confirm that `reslice_until_step(slaposSupportPoints)` triggered by undo does not invoke auto-generation when `sla_points_status` is `UserModified`.
- All user-facing undo/redo entry points (Ctrl+Z / Ctrl+Y keyboard shortcuts **and** the undo/redo toolbar buttons) SHALL exhibit consistent semantics for the Manual Apply snapshot and SHALL NOT crash.

**Non-Goals:**

- Changing the overall two-stack architecture (`main` + `gizmos`).
- Modifying the Enter-key / `editing_mode_apply_changes()` path, which already handles the stack correctly.
- Changing how `TakeSnapshot` behaves for Add, Delete, or Move point operations inside the editing session (those intentionally stay in the gizmo stack).
- Broad rewrite of the BBS undo/redo iterator logic or any `Plater::priv::undo()` / `redo()` change unrelated to the SlaSupports Manual Apply scenario.
- Defining or changing the semantics of `UserModified + 0 support points` (empty Manual Apply). When the user deletes all supports and clicks Apply, `sla_points_status` is persisted as `UserModified` with an empty list; slicing skips auto-generation. Whether this is "explicit no-support" or should reset to auto-generation is a product decision outside this change's scope (candidate: `fix-sla-supports-empty-apply-semantics`).
- Any change to file format, profiles, or public API.

**Crash Safety Scope:**

Crash-free undo/redo is an acceptance constraint for this change, not its primary subject. The Manual Apply scenario exposed an iterator-boundary crash in `Plater::priv::undo()` (`--it_current` UB when `lower_bound` returns `begin()` on a single-snapshot gizmo stack). An initial single pre-check was proven insufficient; the final fix is the **minimum boundary-safe iteration change** required — a full boundary-safe loop (see Decision 4) that checks the iterator boundary after each decrement. This boundary-safe loop SHALL NOT be used as a basis for broader undo/redo architecture changes within this change.

## Decisions

### Decision 1 — Leave → snapshot → re-enter pattern for Manual Apply

**Chosen**: In the Manual Apply button handler, before calling `TakeSnapshot`:
1. Write `m_normal_cache` from `m_editing_cache` (the data to commit).
2. Call `leave_gizmos_stack()` — switches active stack back to main, clears gizmo stack.
3. Call `TakeSnapshot("Support points edit")` — now recorded in main stack.
4. Write committed data to `mo->sla_support_points`.
5. Call `enter_gizmos_stack()` — creates a new "Gizmos-Initial" baseline in the gizmo stack so the session can continue.
6. Re-populate `m_editing_cache` from the now-committed `m_normal_cache` (so `unsaved_changes()` returns false immediately after).
7. Trigger `reslice_until_step(...)` as before.

**Alternatives considered**:

- *Variant A — call `editing_mode_apply_changes()` then immediately `switch_to_editing_mode()`*: Simpler, but `editing_mode_apply_changes()` calls `disable_editing_mode()` which sets `m_editing_mode = false` and closes the editing UI. Re-entering would briefly flash the Auto panel. The chosen approach avoids the UI flicker by staying in manual mode throughout.
- *Variant B — expose a "promote to main stack" utility in Plater*: Cleaner long-term abstraction, but requires changes to Plater's public interface. Deferred as over-engineering for a single call site.

### Decision 2 — Cache sync in `update_after_undo_redo`

**Chosen**: In `GLGizmosManager::update_after_undo_redo`, after `update_data()`, if `m_current == SlaSupports`, explicitly call `reload_cache()` on the gizmo instance. This is unconditional on object-ID change, so it covers the case where the same object is undone.

**Alternatives considered**:

- *Modify `data_changed()` to always reload*: Too broad — `data_changed()` is called on every frame event and a full reload on each call would be expensive and would incorrectly reset in-progress editing-cache state.
- *Clear `m_old_mo_id` in `update_after_undo_redo`*: Forces reload via existing `data_changed()` path. Fragile because it relies on the `m_old_mo_id != mo->id()` guard, which could be bypassed if ID generation changes.

### Decision 3 — Reslice / auto-gen guard

**Chosen**: Verify (no code change expected) that when `RECALCULATE_SLA_SUPPORTS` triggers `reslice_until_step(slaposSupportPoints, true)` after undo, the pipeline reads `sla_points_status`. When status is `UserModified`, `slaposSupportPoints` uses `mo->sla_support_points` directly and does **not** run auto-generation. If this invariant is confirmed by code inspection, no production change is needed — only a test scenario is added to the spec.

### Decision 4 — Boundary-safe iterator handling in Plater::priv::undo()

**Status**: Chosen / Implemented / Verified.

**Context**: The BBS `while (--it_current != snapshots.begin() && !snapshot_modifies_project(*it_current))` loop pre-decrements the iterator before checking the boundary. After `enter_gizmos_stack()` creates a fresh gizmo stack with only the "Gizmos-Initial" baseline snapshot, a Ctrl+Z can put `lower_bound(active_snapshot_time)` at or near `begin()`, causing `--begin()` (UB) and a crash inside `snapshot_modifies_project()`. An initial single pre-check guard (`if (it_current == snapshots.begin()) return;`) was applied but proven insufficient by live testing — a second consecutive undo still crashed.

**Chosen implementation**: Replace the BBS while loop with a fully boundary-safe loop that checks the boundary AFTER each decrement, never dereferencing or decrementing a `begin()` iterator regardless of the snapshot vector state:

```cpp
if (it_current == snapshots.begin()) return;
while (true) {
    --it_current;
    if (it_current == snapshots.begin())
        return;   // hit baseline — nothing to undo to
    if (snapshot_modifies_project(*it_current))
        break;    // found target
}
if (it_current == snapshots.begin()) return;
```

This preserves the existing BBS "skip non-project-modifying snapshots" semantics while making all iterator operations provably safe.

**Verified**: Consecutive undo operations after Manual Apply no longer crash. All primary undo/redo scenarios pass.

**Scope constraint**: This change to `Plater::priv::undo()` is the minimum boundary-safe iteration required to make the SlaSupports Manual Apply scenario crash-free. It does not authorise broader undo/redo architecture changes.

## Risks / Trade-offs

- **Double `leave_gizmos_stack()` if state is inconsistent** → Mitigation: `leave_gizmos_stack()` has an `assert(m_undo_redo_stack_active == &m_undo_redo_stack_gizmos)` guard. The only call site for Manual Apply already holds this invariant (`m_editing_mode == true` ↔ gizmo stack active). Assert will catch any regression.
- **`enter_gizmos_stack()` has `assert(m_undo_redo_stack_gizmos.empty())`** → After `leave_gizmos_stack()` calls `clear()`, the stack is empty before re-entering. Invariant holds.
- **In-session undo after Manual Apply** → After re-entering the gizmo stack, the "Gizmos-Initial" snapshot reflects the applied state. In-session Ctrl+Z is constrained to the gizmo-local stack and cannot reach main-stack Apply snapshots; this is the intended baseline isolation. Whether in-session undo should also produce a UI-visible revert is an active SlaSupports undo routing question tracked by [KB-4] and out of scope for this change.
- **`reslice_until_step` fires during `enter_gizmos_stack()`** → No reslice is triggered by entering; only `take_snapshot("Gizmos-Initial")` is called. Safe.
- **UI undo/redo button vs keyboard shortcut parity** → Both routes call `Plater::priv::undo()` / `redo()`. The boundary-safe loop (Decision 4) applies to both. No separate handling is needed for the UI button path.
- **[KB-3] UI mode difference between Apply paths** → `editing_mode_apply_changes()` (Enter key / gizmo deactivation) exits Manual Editing mode after Apply; the Manual Apply button stays in Manual Editing mode. This change preserves the existing UI-mode difference between the two paths and does not normalize them. Both paths correctly commit support data to the main undo stack.
- **[KB-2] Structure display mode undo/redo refresh** → When SlaSupports is in Structure view mode (`m_show_support_structure = true`), undo/redo may not immediately refresh the support mesh display because the mesh comes from an async reslice (not from `m_normal_cache`). Point sphere display updates correctly via `reload_cache()` + `set_as_dirty()`, but the structure mesh may lag. This is outside this change's scope. Candidate follow-up: `fix-sla-supports-structure-view-undo-refresh`.