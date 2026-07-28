# Tasks

> In-mode step granularity = per-Apply (定義 X). Pending UI edits (Hollow sliders, Drill Add/Delete/Move/Size) stay pending and do NOT snapshot. This change extends the landed apply-only models, it does not replace them.

## 0. Preconditions (verify landed basis)

- [x] 0.1 Confirm `drill-apply-only-undo` is landed/archived (grep `m_working_holes` + `data_changed(is_serializing=true)` rebuild present in `GLGizmoDrill.cpp`)
- [x] 0.2 Confirm Hollow real `on_save/on_load` serialization + `m_pending_owner = nullptr` reset present in `GLGizmoHollow.cpp`
- [x] 0.3 Re-verify current baseline: SlaSupports uses Mechanism B; Hollow/Drill Apply takes a flat snapshot on main (to be re-routed to the sub-stack)

## 1. Shared enter/leave mechanism (Decision A/B)

- [x] 1.1 Add `enter_mode_undo_stack()` / `leave_mode_undo_stack()` to `GLGizmoBase` wrapping `Plater::enter_gizmos_stack()` / `leave_gizmos_stack()`
- [x] 1.2 In `leave_mode_undo_stack()`, use the structural `changed` result (Decision C) to record **at most one** main snapshot; skip entirely on no-op
- [x] 1.3 Enforce ordering: `leave_gizmos_stack()` (switch to main) → then main `TakeSnapshot` (so the collapsed snapshot lands on main)
- [x] 1.4 Add a per-gizmo virtual for the leave-time main snapshot name (Decision B), defaulting via the existing `get_action_snapshot_name()`/`get_gizmo_leaving_text()` family
- [x] 1.5 Make enter/leave idempotent (Decision E.2): `leave` is a no-op when already on main; `enter` collapses an existing session before anchoring a fresh baseline

## 2. Hollow → scoped sub-stack (Decision D)

- [x] 2.1 Override `on_set_state` in `GLGizmoHollow`: On → `enter_mode_undo_stack()`; Off → `leave_mode_undo_stack()`
- [x] 2.2 Provide the Hollow leave snapshot name (`"Hollow"`) via the virtual from 1.4
- [x] 2.3 Keep the Apply-time `TakeSnapshot("Hollow")` but ensure it lands on the active sub-stack; do NOT add per-slider snapshots (pending model preserved)
- [x] 2.4 Verify Hollow `on_load` re-inits pending parameters from restored config after in-mode undo
- [x] 2.5 Add fallback null-guard (Decision E.3) in Hollow data-refresh/render/leave for vanished ModelObject

## 3. Drill → scoped sub-stack (Decision D)

- [x] 3.1 Override `on_set_state` in `GLGizmoDrill`: On → `enter_mode_undo_stack()`; Off → `leave_mode_undo_stack()`
- [x] 3.2 Provide the Drill leave snapshot name (`"Apply drain holes"`) via the virtual from 1.4
- [x] 3.3 Keep the Apply-time `TakeSnapshot("Apply drain holes")` but ensure it lands on the active sub-stack; do NOT snapshot intermediate Add/Delete/Move/Size (pending `m_working_holes` preserved)
- [x] 3.4 Verify Drill `data_changed(is_serializing=true)` rebuilds `m_working_holes` from restored model after in-mode undo (reuse existing path)
- [x] 3.5 Confirm `drill-apply-only-undo` scenarios still hold after re-routing Apply to the sub-stack (MODIFIED delta)
- [x] 3.6 Add fallback null-guard (Decision E.3) in Drill data-refresh/render/leave for vanished ModelObject; also hardened a pre-existing OOB index bug in Drill/Hollow `on_render()` (`objects[get_object_idx()]` with no bounds check)

## 4. SlaSupports unification (Decision D)

- [x] 4.1 Refactor `switch_to_editing_mode` to call `enter_mode_undo_stack()`. `disable_editing_mode` intentionally KEPT calling raw `leave_gizmos_stack()` (documented in-code): `editing_mode_apply_changes()` calls it first then takes its own explicit main snapshot, so routing it through `leave_mode_undo_stack()` would double-snapshot a single Apply
- [x] 4.2 Refactor `commit_manual_edits_keep_editing` to use `leave_mode_undo_stack()`/`enter_mode_undo_stack()`, preserving leave → snapshot → apply → re-enter; proven equivalent to the prior raw calls because the `unsaved_changes()` guard + per-edit snapshotting (Add/Delete/Move support point) guarantee the structural `changed` check agrees
- [x] 4.3 Add fallback null-guard (Decision E.3) in SlaSupports `data_changed`/`on_render` for vanished ModelObject; also hardened the same OOB `objects[get_object_idx()]` bug found in Hollow/Drill
- [x] 4.4 Regression-gate against `sla-supports-apply-undo-stack` scenarios — verified by code-level equivalence analysis (no test harness reaches this GUI path); flagged for Layer 2 manual re-verification before merge

## 5. Structural-mutation containment (Decision E.1) + entry-point audit

- [x] 5.1 At structural-mutation entry, force-collapse an active sub-stack via `GLGizmosManager::reset_all_states()` (existing idempotent idiom, already used elsewhere in the codebase) BEFORE the mutation's snapshot is taken
- [x] 5.2 `Plater::remove_selected` — choke added before `TakeSnapshot("Delete Selected Objects")`
- [x] 5.3 `Plater::priv::delete_object_from_model` — choke added after the cancel-gate, before `TakeSnapshot`
- [x] 5.4 `Plater::priv::delete_all_objects_from_model` — choke added before `TakeSnapshot("Delete All Objects")`
- [x] 5.5 `Plater::priv::remove_curr_plate_all` — choke added before `SingleSnapshot`
- [x] 5.6 `ObjectList::remove` (`wxID_DELETE` bypass path) — choke added as first statement
- [x] 5.7 Instance removal / decrease-instances — `decrease_instances()` is `#if 0`'d out (dead code, not reachable); instance/layer deletion from the Objects panel routes through `ObjectList::remove()`, already choked (5.6)
- [x] 5.8 New / Open / import project — fixed `Plater::priv::reset()` itself (choke was AFTER its `ProjectSeparator` snapshot, a real pre-existing bug: `take_snapshot()` records onto/clears the *active* stack, so a resin mode open during Reset/New Project would wrongly land on or clear the sub-stack). `new_project()` and `load_project()` both call `reset()` before their own snapshot, so both are covered transitively
- [x] 5.9 Reload-from-disk / replace-with-mesh — `reload_from_disk()` had no gate at all; choke added as first statement. `replace_with_stl()` already refuses to run while any gizmo is open (`check_gizmos_closed_except`), verified as pre-existing safe, no change needed
- [x] 5.10 Verified by code-level trace: choke (switch to main) always precedes the mutation's own `TakeSnapshot`, so `[..., (mode result if any), Delete …]` lands correctly on main; empirical confirmation deferred to Layer 2 manual matrix (no test harness reaches this GUI path)

## 6. Tests — Layer 1 (automated, libslic3r)

- [x] 6.1 Extend `tests/libslic3r/test_sla_undo_redo_data.cpp` with a Hollow pending-params (offset/quality/closing/enable) cereal round-trip test, mirroring the exact primitive tuple `GLGizmoHollow::on_save/on_load` serializes (GLGizmoHollow itself isn't linkable into libslic3r tests — wx-dependent)
- [x] 6.2 Drain-holes round-trip already covered by the existing 3 test cases (single hole, vector order/count, empty vector) — no gap, no change needed
- [x] 6.3 **RUN, PASSED** (2026-07-29, `build-resin-dbginfo`, RelWithDebInfo, `SLIC3R_BUILD_TESTS=ON`): `libslic3r_tests.exe "[SLA][UndoRedo][L1]" --order rand` → 5/5 test cases pass (3 pre-existing drain-holes round-trip + 2 new Hollow pending-params round-trip)

### Compile verification (not a formally listed task, recorded here)

- [x] All 12 changed GUI-layer files (6 `.cpp` + `GLGizmoBase.hpp`/`GLGizmoDrill.hpp`/`GLGizmoHollow.hpp`/`GLGizmoSlaSupports.hpp`/`Plater.hpp`) confirmed to compile clean under the real MSVC toolchain: `build-resin-dbginfo/src/RelWithDebInfo/phrozen-orca.exe` built successfully at 2026-07-29 00:15, after all source edits (last edit 2026-07-28 23:44). This supersedes the earlier brace/paren-balance heuristic check as the authoritative compile verification.

## 7. Tests — Layer 2 (manual matrix, GUI)

> **NOT RUN in this session.** This requires a built, running GUI to click through — no build/ directory exists in this checkout, and this environment has no way to drive the live 3D app. All 10 items below are unchecked and need a human (or a `/verify`-style run against a built binary) before merge. Pay particular attention to 7.9: the choke point relies on `GLGizmosManager::reset_all_states()`, but SlaSupports' `on_set_state(Off)` can refuse a synchronous close and instead pop an async "save changes?" confirm dialog when there are unsaved Manual-edit points and the object is still activable at choke time — in that one case the delete snapshot may still land on the SlaSupports sub-stack rather than main (the null-guard prevents a crash, but does not guarantee correct snapshot placement in this specific interaction). This is a known, documented residual gap (see design.md), not a regression from this change — the async-confirm UX itself is pre-existing.

- [ ] 7.1 For each of the 3 modes: enter → sub-stack baseline anchored
- [ ] 7.2 Pending UI edits (Hollow sliders / Drill add-move-size before Apply) create no in-mode step
- [ ] 7.3 In-mode undo across Apply boundaries stops at baseline (over-undo is a safe no-op, no crash)
- [ ] 7.4 In-mode redo only replays in-session Apply steps
- [ ] 7.5 Multiple Applies collapse to exactly one main snapshot on leave; one Ctrl+Z restores pre-entry state
- [ ] 7.6 No-op session (no Apply) records nothing on leave
- [ ] 7.7 Applies undone to baseline records nothing on leave
- [ ] 7.8 After in-mode undo, pending working state (Drill holes / Hollow params) matches restored model
- [ ] 7.9 Structural mutation while in mode: delete focused object (Delete key, toolbar, and `wxID_DELETE`), delete-all, plate-clear, load project — no crash, snapshot on main. Also specifically test: delete the focused object while SlaSupports has unsaved Manual-edit points (known residual gap above)
- [ ] 7.10 Direct mode→mode switch (Support ↔ Hollow ↔ Drill) collapses each session cleanly, no edit bleed

## 8. Wrap-up

- [x] 8.1 Confirmed by code review: non-mode undo/redo machinery (`Plater::priv::undo/redo/undo_redo_to`, `UndoRedo::Stack`, FDM gizmos, Mechanism A) was not modified; the choke point (`reset_all_states()`) is idempotent and a no-op when no resin mode is open, so non-mode operations are structurally unaffected. Empirical confirmation still pending via Layer 2 (7.x).
- [x] 8.2 Structural-mutation entry-point audit — final coverage status:

  | Entry point | Coverage |
  |---|---|
  | `Plater::remove_selected` | Choke added |
  | `Plater::priv::remove` | Choke added |
  | `Plater::priv::delete_object_from_model` | Choke added |
  | `Plater::priv::delete_all_objects_from_model` | Choke added |
  | `Plater::priv::remove_curr_plate_all` | Choke added |
  | `ObjectList::remove` (`wxID_DELETE`) | Choke added |
  | Instance / layer removal | Covered transitively — routes through `ObjectList::remove` |
  | `decrease_instances()` | N/A — dead code (`#if 0`), not reachable |
  | New project (`new_project`) | Covered transitively via fixed `Plater::priv::reset()` |
  | Open/Load project (`load_project`) | Covered transitively via fixed `Plater::priv::reset()` |
  | `Plater::priv::reset()` itself | Fixed directly — choke was missing entirely; also fixed ordering (choke now precedes the `ProjectSeparator` snapshot) |
  | `reload_from_disk()` | Choke added (had no gate at all) |
  | `replace_with_stl()` | No change needed — already refuses to run while any gizmo is open (`check_gizmos_closed_except`), verified pre-existing safe |
  | Residual: SlaSupports delete-with-unsaved-edits async-confirm | NOT fully covered — documented known gap (see §7 note); mitigated by null-guard (no crash) but snapshot may land on wrong stack in this one interaction |

- [x] 8.3 Accepted residuals, noted as follow-up candidates (not fixed in this change):
  - Decision C: editing a value and reverting it to the exact baseline value before leaving records a redundant (content-identical) main-stack snapshot, because the no-op check is structural (timestamp-based), not content-diff-based.
  - SlaSupports delete-with-unsaved-Manual-edits-and-async-confirm interaction (see §7 note and audit table above) — would need either a UX decision (force-discard vs. block-delete-until-resolved) or making the delete path await the async dialog's resolution; both are out of scope for this change.
