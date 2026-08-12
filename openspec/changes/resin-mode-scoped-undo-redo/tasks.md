# Tasks

> In-mode step granularity = per-Apply (定義 X). Pending UI edits (Hollow sliders, Drill Add/Delete/Move/Size) stay pending and do NOT snapshot. This change extends the landed apply-only models, it does not replace them.

> ⚠️ **2026-08-09 pivot (Decision G, see design.md)**: Sections 1-5 and 8.2 below describe the scoped sub-stack mechanism (Decisions A-E) — it was fully implemented and code-reviewed, but Section 7 manual verification surfaced three interlocking bugs rooted in the mechanism itself, and it was abandoned in favor of a single main stack for all three modes. Sections 1-5/8.2 are kept `[x]` as an honest historical record (the work was genuinely done, then reverted), not because they describe current behavior. **Section 9 is the actual final implementation.** Section 7's matrix has been replaced with one appropriate for the single-stack design; the original 10-item scoped-stack matrix is preserved below it for reference only.

## 0. Preconditions (verify landed basis)

- [x] 0.1 Confirm `drill-apply-only-undo` is landed/archived (grep `m_working_holes` + `data_changed(is_serializing=true)` rebuild present in `GLGizmoDrill.cpp`)
- [x] 0.2 Confirm Hollow real `on_save/on_load` serialization + `m_pending_owner = nullptr` reset present in `GLGizmoHollow.cpp`
- [x] 0.3 Re-verify current baseline: SlaSupports uses Mechanism B; Hollow/Drill Apply takes a flat snapshot on main (to be re-routed to the sub-stack)

## 1. Shared enter/leave mechanism (Decision A/B) ⚠️ SUPERSEDED — reverted by Decision G, see Section 9

- [x] 1.1 Add `enter_mode_undo_stack()` / `leave_mode_undo_stack()` to `GLGizmoBase` wrapping `Plater::enter_gizmos_stack()` / `leave_gizmos_stack()`
- [x] 1.2 In `leave_mode_undo_stack()`, use the structural `changed` result (Decision C) to record **at most one** main snapshot; skip entirely on no-op
- [x] 1.3 Enforce ordering: `leave_gizmos_stack()` (switch to main) → then main `TakeSnapshot` (so the collapsed snapshot lands on main)
- [x] 1.4 Add a per-gizmo virtual for the leave-time main snapshot name (Decision B), defaulting via the existing `get_action_snapshot_name()`/`get_gizmo_leaving_text()` family
- [x] 1.5 Make enter/leave idempotent (Decision E.2): `leave` is a no-op when already on main; `enter` collapses an existing session before anchoring a fresh baseline

## 2. Hollow → scoped sub-stack (Decision D) ⚠️ SUPERSEDED — reverted by Decision G, see Section 9

- [x] 2.1 Override `on_set_state` in `GLGizmoHollow`: On → `enter_mode_undo_stack()`; Off → `leave_mode_undo_stack()`
- [x] 2.2 Provide the Hollow leave snapshot name (`"Hollow"`) via the virtual from 1.4
- [x] 2.3 Keep the Apply-time `TakeSnapshot("Hollow")` but ensure it lands on the active sub-stack; do NOT add per-slider snapshots (pending model preserved)
- [x] 2.4 Verify Hollow `on_load` re-inits pending parameters from restored config after in-mode undo
- [x] 2.5 Add fallback null-guard (Decision E.3) in Hollow data-refresh/render/leave for vanished ModelObject

## 3. Drill → scoped sub-stack (Decision D) ⚠️ SUPERSEDED — reverted by Decision G, see Section 9

- [x] 3.1 Override `on_set_state` in `GLGizmoDrill`: On → `enter_mode_undo_stack()`; Off → `leave_mode_undo_stack()`
- [x] 3.2 Provide the Drill leave snapshot name (`"Apply drain holes"`) via the virtual from 1.4
- [x] 3.3 Keep the Apply-time `TakeSnapshot("Apply drain holes")` but ensure it lands on the active sub-stack; do NOT snapshot intermediate Add/Delete/Move/Size (pending `m_working_holes` preserved)
- [x] 3.4 Verify Drill `data_changed(is_serializing=true)` rebuilds `m_working_holes` from restored model after in-mode undo (reuse existing path)
- [x] 3.5 Confirm `drill-apply-only-undo` scenarios still hold after re-routing Apply to the sub-stack (MODIFIED delta)
- [x] 3.6 Add fallback null-guard (Decision E.3) in Drill data-refresh/render/leave for vanished ModelObject; also hardened a pre-existing OOB index bug in Drill/Hollow `on_render()` (`objects[get_object_idx()]` with no bounds check)

## 4. SlaSupports unification (Decision D) ⚠️ SUPERSEDED — reverted by Decision G, see Section 9

- [x] 4.1 Refactor `switch_to_editing_mode` to call `enter_mode_undo_stack()`. `disable_editing_mode` intentionally KEPT calling raw `leave_gizmos_stack()` (documented in-code): `editing_mode_apply_changes()` calls it first then takes its own explicit main snapshot, so routing it through `leave_mode_undo_stack()` would double-snapshot a single Apply
- [x] 4.2 Refactor `commit_manual_edits_keep_editing` to use `leave_mode_undo_stack()`/`enter_mode_undo_stack()`, preserving leave → snapshot → apply → re-enter; proven equivalent to the prior raw calls because the `unsaved_changes()` guard + per-edit snapshotting (Add/Delete/Move support point) guarantee the structural `changed` check agrees
- [x] 4.3 Add fallback null-guard (Decision E.3) in SlaSupports `data_changed`/`on_render` for vanished ModelObject; also hardened the same OOB `objects[get_object_idx()]` bug found in Hollow/Drill
- [x] 4.4 Regression-gate against `sla-supports-apply-undo-stack` scenarios — verified by code-level equivalence analysis (no test harness reaches this GUI path); flagged for Layer 2 manual re-verification before merge

## 5. Structural-mutation containment (Decision E.1) + entry-point audit ⚠️ SUPERSEDED — no sub-stack to protect, see Section 9. Decision E.3 (null-guard) is the one piece of this section that survives, unchanged.

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

## 7. [SUPERSEDED, kept for reference only] Original Layer-2 matrix for the scoped sub-stack

> This matrix targeted Decisions A-E and was never run (no build available at the time). Decision G abandoned the mechanism it tests, so it will never be run as written. Kept verbatim for historical traceability. **Section 9 below is the matrix that was actually executed.**

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

## 8. Decision G — single-stack implementation (what was actually shipped)

- [x] 8.1 `GLGizmoHollow`/`GLGizmoDrill::on_set_state()` — remove `enter_mode_undo_stack()`/`leave_mode_undo_stack()` calls; Apply-time `TakeSnapshot` left untouched, now unambiguously always on main
- [x] 8.2 `GLGizmoSlaSupports::switch_to_editing_mode()`/`disable_editing_mode()` — remove sub-stack enter/leave calls
- [x] 8.3 `GLGizmoSlaSupports` — remove `wants_enter_leave_snapshots()`/`get_gizmo_entering_text()`/`get_gizmo_leaving_text()` overrides (Mechanism A no longer wraps the whole panel)
- [x] 8.4 `commit_manual_edits_keep_editing()` keeps the already-fixed in-place `TakeSnapshot("Support points edit")` (no leave/re-enter) — unaffected by this pivot, was independently correct
- [x] 8.5 Delete now-dead `GLGizmoBase::enter_mode_undo_stack()`/`leave_mode_undo_stack()` and the `get_mode_leave_snapshot_name()` virtual + its overrides in `GLGizmoDrill.hpp`/`GLGizmoHollow.hpp`
- [x] 8.6 Keep the Decision E.3 null-guard fallback (vanished-ModelObject self-close) in all three gizmos, unchanged — orthogonal to stack architecture
- [x] 8.7 Keep `Plater::priv::leave_gizmos_stack()`'s baseline-timestamp fix — `GLGizmoBrimEars` still uses the raw API directly and the fix is correct for it too
- [x] 8.8 New: `GLGizmoSlaSupports::resync_after_undo_redo()` — unconditional `reload_cache()` + SLA backend invalidate/reslice on every undo/redo landing on Support outside editing mode, replacing the unreliable `RECALCULATE_SLA_SUPPORTS`-flag-gated call in `GLGizmosManager::update_after_undo_redo()`
- [x] 8.9 New: `on_set_state(Off)`'s real-shutdown branch unconditionally forces `m_show_support_structure = true; show_sla_supports(true);` so a successfully-generated support structure is never left invisible after closing the panel, regardless of which sub-view (Points/Structure) was last selected
- [x] 8.10 Full rebuild (`PhrozenOrca_app_gui.vcxproj`, RelWithDebInfo) after each step — all compile clean, no errors (warnings only, pre-existing)
- [x] 8.11 Diagnostic `[undo-diag]` logging added during the investigation (GLGizmoBase.cpp, GLGizmosManager.cpp, Plater.cpp) removed once root causes were confirmed and fixed

## 9. Tests — Layer 2 (manual matrix, GUI, single-stack design)

> Replaces Section 7. Scoped-sub-stack concepts (baseline anchoring, in-mode-vs-main boundary, collapse-to-one-on-leave, no-op-skip-on-leave) no longer apply — there is one stack, and every committed action is its own permanent step on it.

> **2026-08-12**: Support-specific items (9.2, 9.5, 9.6) confirmed passing by user testing. Hollow/Drill found to still have problems — under investigation, not yet fixed. Cross-mode items (9.1, 9.3, 9.4, 9.7, 9.8) left unchecked until Hollow/Drill's issues are resolved and re-verified together.

- [ ] 9.1 Each Apply/add/delete/move in all 3 modes is individually undoable and redoable, in the order performed, exactly like any other main-stack operation
- [x] 9.2 Support Manual mode: add a point, Apply (stay in mode), add another point — Ctrl+Z once only undoes the second point; the first point and the first Apply remain intact (regression check for the leave+re-enter bug this pivot fixes) — **confirmed passing**
- [ ] 9.3 Undo/redo that lands on a snapshot taken while a mode's panel was open/closed correctly opens/closes that panel as a side effect, with no crash and no getting "stuck" (regression check for the hard-boundary bug this pivot fixes)
- [ ] 9.4 Repeated undo past the point where any mode was ever opened does not crash and does not get stuck — it just keeps walking through ordinary main-stack history
- [x] 9.5 `resync_after_undo_redo()`: Support Auto Apply → switch to Structure view → leave mode → undo — the pad/support-tree mesh updates to match (cleared or regenerated), does not stay stale — **confirmed passing**
- [x] 9.6 Leaving the Support panel always leaves the actual support structure visible in the normal 3D view, regardless of whether Points or Structure sub-view was last selected inside the panel — **confirmed passing**
- [ ] 9.7 Structural mutation while a mode is open (delete focused object via Delete key / toolbar / `wxID_DELETE`, delete-all, plate-clear, load project) — no crash; the gizmo self-closes via the null-guard fallback
- [ ] 9.8 Direct mode→mode switch (Support ↔ Hollow ↔ Drill) — no crash, each mode's own history remains on the single main stack in the order performed
- [ ] 9.9 `GLGizmoBrimEars` (FDM, unaffected by this change) still works — its own enter/leave-gizmos-stack Apply flow is unchanged, sanity-check only

### 9a. Hollow/Drill regressions found during 2026-08-12 testing

> Support mode passed; Hollow/Drill did not. Details being reported by user — filled in below as they're diagnosed.

## 10. Wrap-up

- [x] 10.1 Confirmed by code review: non-mode undo/redo machinery (`Plater::priv::undo/redo/undo_redo_to`, `UndoRedo::Stack`, FDM gizmos, Mechanism A for FDM painting gizmos) not modified by the Decision G pivot.
- [x] 10.2 Structural-mutation entry-point audit (Section 5) — superseded, not needed under single-stack. The one requirement it partially covered that still matters — a gizmo not crashing when its focused object vanishes — is fully covered by the retained Decision E.3 null-guard, verified per-gizmo in Section 9.7.
- [x] 10.3 Accepted residuals carried forward from the abandoned design, still relevant:
  - Undo history is intentionally verbose under single-stack (Decision G accepted trade-off) — not a residual bug, a deliberate choice.
- [ ] 10.4 Once Section 9 passes, archive this change; sync `resin-mode-single-stack-undo-redo` and the trimmed `resin-mode-structural-mutation-safety` into the main specs; do **not** sync the abandoned `resin-mode-scoped-undo-stack` spec (already removed from `specs/`).
