# Tasks

> In-mode step granularity = per-Apply (定義 X). Pending UI edits (Hollow sliders, Drill Add/Delete/Move/Size) stay pending and do NOT snapshot. This change extends the landed apply-only models, it does not replace them.

## 0. Preconditions (verify landed basis)

- [ ] 0.1 Confirm `drill-apply-only-undo` is landed/archived (grep `m_working_holes` + `data_changed(is_serializing=true)` rebuild present in `GLGizmoDrill.cpp`)
- [ ] 0.2 Confirm Hollow real `on_save/on_load` serialization + `m_pending_owner = nullptr` reset present in `GLGizmoHollow.cpp`
- [ ] 0.3 Re-verify current baseline: SlaSupports uses Mechanism B; Hollow/Drill Apply takes a flat snapshot on main (to be re-routed to the sub-stack)

## 1. Shared enter/leave mechanism (Decision A/B)

- [ ] 1.1 Add `enter_mode_undo_stack()` / `leave_mode_undo_stack()` to `GLGizmoBase` wrapping `Plater::enter_gizmos_stack()` / `leave_gizmos_stack()`
- [ ] 1.2 In `leave_mode_undo_stack()`, use the structural `changed` result (Decision C) to record **at most one** main snapshot; skip entirely on no-op
- [ ] 1.3 Enforce ordering: `leave_gizmos_stack()` (switch to main) → then main `TakeSnapshot` (so the collapsed snapshot lands on main)
- [ ] 1.4 Add a per-gizmo virtual for the leave-time main snapshot name (Decision B), defaulting via the existing `get_action_snapshot_name()`/`get_gizmo_leaving_text()` family
- [ ] 1.5 Make enter/leave idempotent (Decision E.2): `leave` is a no-op when already on main; `enter` collapses an existing session before anchoring a fresh baseline

## 2. Hollow → scoped sub-stack (Decision D)

- [ ] 2.1 Override `on_set_state` in `GLGizmoHollow`: On → `enter_mode_undo_stack()`; Off → `leave_mode_undo_stack()`
- [ ] 2.2 Provide the Hollow leave snapshot name (`"Hollow"`) via the virtual from 1.4
- [ ] 2.3 Keep the Apply-time `TakeSnapshot("Hollow")` but ensure it lands on the active sub-stack; do NOT add per-slider snapshots (pending model preserved)
- [ ] 2.4 Verify Hollow `on_load` re-inits pending parameters from restored config after in-mode undo
- [ ] 2.5 Add fallback null-guard (Decision E.3) in Hollow data-refresh/render/leave for vanished ModelObject

## 3. Drill → scoped sub-stack (Decision D)

- [ ] 3.1 Override `on_set_state` in `GLGizmoDrill`: On → `enter_mode_undo_stack()`; Off → `leave_mode_undo_stack()`
- [ ] 3.2 Provide the Drill leave snapshot name (`"Apply drain holes"`) via the virtual from 1.4
- [ ] 3.3 Keep the Apply-time `TakeSnapshot("Apply drain holes")` but ensure it lands on the active sub-stack; do NOT snapshot intermediate Add/Delete/Move/Size (pending `m_working_holes` preserved)
- [ ] 3.4 Verify Drill `data_changed(is_serializing=true)` rebuilds `m_working_holes` from restored model after in-mode undo (reuse existing path)
- [ ] 3.5 Confirm `drill-apply-only-undo` scenarios still hold after re-routing Apply to the sub-stack (MODIFIED delta)
- [ ] 3.6 Add fallback null-guard (Decision E.3) in Drill data-refresh/render/leave for vanished ModelObject

## 4. SlaSupports unification (Decision D)

- [ ] 4.1 Refactor `switch_to_editing_mode` / `disable_editing_mode` to call the shared enter/leave methods
- [ ] 4.2 Refactor `commit_manual_edits_keep_editing` to use the shared mechanism while preserving the leave → snapshot → apply → re-enter (commit-and-stay) flow
- [ ] 4.3 Add fallback null-guard (Decision E.3) in SlaSupports for vanished ModelObject on leave
- [ ] 4.4 Regression-gate against `sla-supports-apply-undo-stack` scenarios (Manual Apply, gizmo switch, Ctrl+Z / UI Undo, no-crash) — behavior unchanged

## 5. Structural-mutation containment (Decision E.1) + entry-point audit

- [ ] 5.1 At structural-mutation entry, force-collapse an active sub-stack (switch active back to main) BEFORE the mutation's snapshot is taken
- [ ] 5.2 `Plater::remove_selected` — covered
- [ ] 5.3 `Plater::priv::delete_object_from_model` — covered
- [ ] 5.4 `Plater::priv::delete_all_objects_from_model` — covered
- [ ] 5.5 `Plater::priv::remove_curr_plate_all` — covered
- [ ] 5.6 `ObjectList::remove` (`wxID_DELETE` bypass path) — covered
- [ ] 5.7 Instance removal / decrease-instances — covered
- [ ] 5.8 New / Open / import project — covered
- [ ] 5.9 Reload-from-disk / replace-with-mesh — covered
- [ ] 5.10 Verify delete-in-mode produces a clean history: `[..., (mode result if any), Delete …]`, with the delete snapshot on main

## 6. Tests — Layer 1 (automated, libslic3r)

- [ ] 6.1 Extend `tests/libslic3r/test_sla_undo_redo_data.cpp` with Hollow params (offset/quality/closing/enable) cereal round-trip
- [ ] 6.2 Extend with drain-holes round-trip coverage (if not already covered by `fix-sla-undo-redo`)
- [ ] 6.3 Run `libslic3r_tests` and confirm all pass

## 7. Tests — Layer 2 (manual matrix, GUI)

- [ ] 7.1 For each of the 3 modes: enter → sub-stack baseline anchored
- [ ] 7.2 Pending UI edits (Hollow sliders / Drill add-move-size before Apply) create no in-mode step
- [ ] 7.3 In-mode undo across Apply boundaries stops at baseline (over-undo is a safe no-op, no crash)
- [ ] 7.4 In-mode redo only replays in-session Apply steps
- [ ] 7.5 Multiple Applies collapse to exactly one main snapshot on leave; one Ctrl+Z restores pre-entry state
- [ ] 7.6 No-op session (no Apply) records nothing on leave
- [ ] 7.7 Applies undone to baseline records nothing on leave
- [ ] 7.8 After in-mode undo, pending working state (Drill holes / Hollow params) matches restored model
- [ ] 7.9 Structural mutation while in mode: delete focused object (Delete key, toolbar, and `wxID_DELETE`), delete-all, plate-clear, load project — no crash, snapshot on main
- [ ] 7.10 Direct mode→mode switch (Support ↔ Hollow ↔ Drill) collapses each session cleanly, no edit bleed

## 8. Wrap-up

- [ ] 8.1 Confirm non-mode undo/redo unchanged (move/rotate/scale/delete outside any mode)
- [ ] 8.2 Update the structural-mutation audit table with final coverage status
- [ ] 8.3 Note the accepted residual (edit-then-revert-to-baseline-value redundant snapshot, Decision C) as a follow-up candidate
