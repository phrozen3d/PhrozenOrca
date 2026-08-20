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

> **2026-08-12**: Support-specific items (9.2, 9.5, 9.6) confirmed passing. Hollow initially failed (see 9a — two real bugs found and fixed, unrelated to the stack pivot itself). After both fixes, user confirmed Hollow undo/redo stable, and separately confirmed Drill undo/redo stable, no crash. 9.3 marked passing on that basis (the Hollow crash was exactly this scenario). 9.7 tested next (delete-object-while-mode-open, the original motivating crash history) and found a *third*, distinct crash — see 9b — now fixed and confirmed. During that same investigation, a *fourth* issue was found and fixed: Hollow/Drill results going stale/missing after undo/redo (root cause understood, not just patched — see 9b). 9.1/9.4/9.8/9.9 still not individually walked through as discrete test steps; general stability across many undo/redo cycles in all 3 modes has been observed throughout, but these specific scenarios haven't been deliberately exercised.
>
> **2026-08-13**: 9.5 re-broke twice more after the above (see 9c/9d/9e/9f) before its real root cause — three gizmos' `resync_after_undo_redo()` each independently force-restarting the single shared background process, capping it at whichever was queued last — was found and fixed (9f). **User confirmed single-mode undo/redo (Support alone, Hollow alone, Drill alone) stable, no crashes, and explicitly decided to stop this change here** — 9.1/9.4/9.8/9.9 (broader/multi-mode matrix items) and the multi-mode-sequence gap noted in 9f remain untested/unfixed, deliberately deferred to a future separate spec rather than expanding this change's scope further.

- [ ] 9.1 Each Apply/add/delete/move in all 3 modes is individually undoable and redoable, in the order performed, exactly like any other main-stack operation
- [x] 9.2 Support Manual mode: add a point, Apply (stay in mode), add another point — Ctrl+Z once only undoes the second point; the first point and the first Apply remain intact (regression check for the leave+re-enter bug this pivot fixes) — **confirmed passing**
- [x] 9.3 Undo/redo that lands on a snapshot taken while a mode's panel was open/closed correctly opens/closes that panel as a side effect, with no crash and no getting "stuck" — **confirmed passing** (Hollow: undo right after processing completes + leaving the mode, the exact scenario that crashed pre-fix, now stable; Drill: confirmed stable, no crash)
- [ ] 9.4 Repeated undo past the point where any mode was ever opened does not crash and does not get stuck — it just keeps walking through ordinary main-stack history
- [x] 9.5 `resync_after_undo_redo()`: Support Auto Apply → switch to Structure view → leave mode → undo — the pad/support-tree mesh updates to match (cleared or regenerated), does not stay stale — **regressed by 9c/9d's changes; 9e's fix was insufficient (see 9f for the real root cause: three independent reslice_until_step() calls racing/capping the shared background process); 9f's fix confirmed passing by user (2026-08-13), single-mode Support/Hollow/Drill undo/redo all stable, no crashes**
- [x] 9.6 Leaving the Support panel always leaves the actual support structure visible in the normal 3D view, regardless of whether Points or Structure sub-view was last selected inside the panel — **confirmed passing**
- [x] 9.7 Structural mutation while a mode is open — **confirmed passing, all variants**: delete focused object (found and fixed a crash — see 9b, `BackgroundSlicingProcess::m_current_plate` going dangling when undo restores an older `PartPlateList` after the plate's only object is deleted — this was the scenario the original crash history `e367bb0025` that motivated this whole change was about); delete-all / plate-clear / load-project while Hollow or Support is open — no crash, no fix needed for these variants.
- [ ] 9.8 Direct mode→mode switch (Support ↔ Hollow ↔ Drill) — no crash, each mode's own history remains on the single main stack in the order performed
- [ ] 9.9 `GLGizmoBrimEars` (FDM, unaffected by this change) still works — its own enter/leave-gizmos-stack Apply flow is unchanged, sanity-check only

### 9a. Hollow/Drill regressions found during 2026-08-12 testing

**Hollow — two real bugs found and fixed, both unrelated to the Decision G stack pivot itself:**

1. **Hang** (blocked all Hollow testing initially): `sla::generate_interior()` ran a lossless `its_quadric_edge_collapse()` pass on the marching-cubes interior mesh, which could take a very long time (and made Cancel unresponsive, since the cancel check inside QEC only fires every 16 outer iterations and a single expensive candidate edge blocks it). Root-caused via direct diff against upstream PrusaSlicer (`C:\dev\PrusaSlicer\PrusaSlicer`): (a) `QuadricEdgeCollapse::is_flipped()` was stricter than upstream's fixed version (rejected more valid collapses without comparing against the original triangle's own ugliness first) — aligned to upstream; (b) upstream's current `generate_interior()` doesn't run this QEC pass at all — removed it, kept the cheap `its_compactify_vertices`/`its_merge_vertices` cleanup. User confirmed: Hollow itself now completes quickly.
2. **Crash on undo** (found after fixing #1, since that was the first time Hollow ever completed and this scenario became reachable): `BackgroundSlicingProcess::apply()` called `new_config.apply(*m_current_plate->config())` with the default `ignore_nonexistent=false`; an uncaught `UnknownOptionException` from the background slicing thread aborted the whole process. Fixed: explicit `ignore_nonexistent=true` (a per-plate override config isn't guaranteed to only contain keys valid for the current technology across an undo/redo restore) + added a `m_current_plate` null-guard matching the existing pattern in `start()`. User confirmed: undo right after Hollow completes no longer crashes.

**Follow-up identified, explicitly deferred (not part of this change):** after fix #1, a *second*, independent slow step became visible — `drill_holes()`'s `remove_inside_triangles()` stage ("Drilling holes into model", progress stuck for a while). Traced to `get_distance()`'s culling/early-exit formula also having diverged from upstream (part of the same broader `Interior`-struct redesign upstream did — `iso_surface`/`thickness`/`full_narrowb` vs this fork's `nb_in`/`nb_out`/`thickness`/`voxel_scale`/`closing_distance`). Porting `get_distance()` alone without migrating the whole `Interior` population logic in `generate_interior()` risks silently wrong culling (bad hollow-wall topology, not just slow) rather than just being slow — assessed as too large/risky to fold into this change. Needs its own scoped change if pursued, gated on confirming whether PrusaSlicer's *drilling* stage specifically (not just interior generation) is actually fast for a comparable case.

**Drill**: user confirmed undo/redo stable, no crash. No Drill-specific bugs found this round.

### 9b. Two more findings during 9.7 testing (delete object while a mode is open, then undo)

**3. Crash on undo after deleting the mode's focused object** (`BackgroundSlicingProcess::apply()`, access violation reading `0xFFFFFFFFFFFFFFFF`): `Plater::priv::update_after_undo_redo()` triggers a background-process apply without first refreshing `BackgroundSlicingProcess::m_current_plate` — every *other* call site that touches the background process after a plate-affecting change calls `update_slice_context_to_current_plate()` first; this one didn't. Deleting the only object on a plate then undoing rebuilds/replaces the `PartPlateList`'s `PartPlate` objects, leaving the raw `m_current_plate` pointer dangling (not null — the earlier `!m_current_plate` guard from 9a's bug #2 didn't catch this, since the pointer's value isn't null, it just points at freed memory). Fixed: added the missing `update_slice_context_to_current_plate()` refresh at the top of `update_after_undo_redo()`, before anything can trigger the background process. User confirmed: delete the focused object while Support mode is open (Structure view active) → undo, no longer crashes.

**4. Hollow/Drill results go stale/missing after undo/redo** (not a crash — a correctness gap, same class as Support's 9a/9.5 bug but never fixed for these two gizmos): reproduced independently of the crash above — Hollow Apply → leave → Drill add 3 points → Apply → leave → undo once lands back in Drill with the 3 points gone **and the Hollow result also gone**; undo again lands in Hollow, and re-running Hollow shows the progress bar starting from the Drilling stage (i.e. the whole `SLAPrintObject` got discarded and rebuilt, not just the Drill step). Root cause fully traced (see design.md Decision H): `SLAPrintObject::m_hollowing_data` (the actual carved/drilled mesh) is a backend-only derived cache that was never part of `Model` to begin with — undo/redo correctly restores the *source of truth* (original mesh + `hollowing_*` config + `sla_drain_holes`), but `SLAPrint::apply()`'s top-level `model.id() != m_model.id()` check discards and rebuilds every `SLAPrintObject` on essentially every undo/redo, regardless of whether Hollow/Drill-relevant data changed at all. This is the exact same category of gap `resync_after_undo_redo()` closed for Support — Hollow and Drill just never got their own copy of it. Fixed: added `GLGizmoHollow::resync_after_undo_redo()` (forces `reslice_until_step(slaposHollowing)`) and `GLGizmoDrill::resync_after_undo_redo()` (forces `reslice_until_step(slaposDrillHoles)`), wired into `GLGizmosManager::update_after_undo_redo()` alongside the existing Support case. Pending-state refresh (Hollow's `on_load`/`data_changed`, Drill's `m_working_holes` rebuild) was already correct and untouched — only the "tell the backend to recompute" step was missing. **User confirmed passing**: undo twice after Hollow→Drill correctly restores Drill's then Hollow's results in turn, each visible again as expected.

### 9c. Gap found in the 9a/9b fix: `resync_after_undo_redo()` didn't fire when landing on a state where no relevant gizmo is open

**Fixed (2026-08-12), user-chosen direction (Decision K's "narrower" option). Awaiting re-test confirmation.**

Reproduction: Hollow Apply → leave → Drill add 3 points → Apply → leave → undo once (lands back in Drill mode, 3 points gone, Hollow result correctly still present — this part matches 9b's fix). **Then redo immediately**: the app leaves Drill mode (matches the captured state — Drill had already been left when this position was recorded) but **both** the Drill holes **and** the Hollow result went missing, even though the *model data* (config + `sla_drain_holes`) was correctly restored by the redo.

Root cause: `GLGizmosManager::update_after_undo_redo()` only called a gizmo's `resync_after_undo_redo()` when that gizmo was `m_current` (the currently active one) *after* the undo/redo landed. In this redo, the restored snapshot's captured gizmo state is "no gizmo open" (since the user had already left Drill before this position was recorded) — so neither the Hollow nor the Drill branch fired, even though Decision H's backend-wipe-on-every-undo/redo still applies regardless of gizmo state.

This was a scope gap in the fix's *design*, not a one-off miss: the underlying problem (Decision H) is scoped to "landed on a state that has hollow/drill/support data," but the original fix (Decision I) was scoped to "landed on a state where the relevant gizmo happens to be currently open." Those two scopes only partially overlapped.

**Fix applied**: `GLGizmosManager::update_after_undo_redo()` now decides whether to resync based on the restored, currently-selected `ModelObject`'s actual data (`hollowing_enable`, non-empty `sla_drain_holes`, `generate_support`/existing `sla_support_points`) instead of `m_current`. Also fixed `GLGizmoSlaSupports::resync_after_undo_redo()`, which previously early-returned entirely when `m_c->selection_info()` was unavailable (i.e. whenever Support isn't the active gizmo) — never reaching `reslice_until_step()` at all; now the `m_c`-dependent parts (cache reload, explicit support-points invalidate) are skipped gracefully but `reslice_until_step()` (which has its own `m_c`-independent fallback via `m_parent.get_selection()`) always still runs. See design.md Decision K for the full writeup including the option not taken (fixing `SLAPrint::apply()`'s `model.id()` root cause) and why.

### 9d. Regression introduced by 9c's fix, found immediately on re-test, now fixed

**Fixed. Awaiting re-test confirmation** (both `RelWithDebInfo` and `Release` configurations rebuilt).

9c's fix made things *worse*, not better: after it, undo-once (Hollow Apply → leave → Drill add 3 → Apply → leave → undo) made the Hollow result disappear too — previously (pre-9c) that case worked correctly, if only by accident (see below). Redo was still broken as before.

Root cause: `GLGizmoHollow::resync_after_undo_redo()` requested `reslice_until_step(slaposHollowing)`. But `SLAPrintObject::get_mesh_to_print()`/`get_mesh_to_slice()` — the mesh actually used for display/print — are gated on `is_step_done(slaposDrillHoles)`, **not** `slaposHollowing`, even when there are zero drain holes (`drill_holes()`'s `!needs_drilling` branch still builds the final carved/trimmed mesh whenever the object is hollowed). Every other trigger point already in `GLGizmoHollow.cpp` (the Apply button; see the file's own top-of-file comment "hollow_mesh() removed, use reslice_until_step(slaposDrillHoles)") already correctly requests `slaposDrillHoles` — `resync_after_undo_redo()` was the one place written to request the wrong, earlier step, and so never actually produced anything visible.

Why undo-once looked fine *before* 9c's fix, purely by accident: the old `m_current`-gated code happened to land in Drill mode after that specific undo (since Drill was the panel most recently open), so it went through *Drill's* resync (`slaposDrillHoles`, correctly targeted) — not Hollow's. The bug in Hollow's own resync method was there all along, just never exercised by that particular repro until 9c's fix started calling it based on data instead of on which gizmo happened to be active.

### 9e. Third regression, found on re-test of Support specifically (2026-08-13), attempted fix insufficient — see 9f for the real root cause

**Attempted fix applied but insufficient on its own (superseded by 9f). Kept here as it did fix a genuine, separate bug.**

Reproduction (this is exactly 9.5's own scenario): Load cube → Support mode → Apply → switch to Structure view → leave mode → undo. Support pad stayed visibly stuck on screen (matches 9.5's original failure mode), **plus two symptoms not previously seen**: the model itself floated up in mid-air, and a follow-up redo failed to regrow the support tree at all.

Root cause: `GLGizmoSlaSupports::resync_after_undo_redo()`'s `!has_support_data` branch (added by 9c/Decision K to handle "undo/redo lands on a state with no support data at all") called `sync_generate_support_for_object(mo, false)` and `clear_support_volumes()`, then `return`ed — **never calling `reslice_until_step()`**. Neither of the two calls it did make reaches the backend: `sync_generate_support_for_object()` only mutates `ModelObject::config` in memory, and `clear_support_volumes()` only touches `GLGizmoSlaBase::m_volumes` — this gizmo's own private mesh cache, read only while its panel is actively open/rendering. Once the user has left the mode (exactly this repro), nothing reads `m_volumes` any more.

The pad actually still visible after leaving the mode is a normal `GLCanvas3D` scene volume, added by `GLCanvas3D::_load_sla_shells()`, driven purely by `SLAPrintObject::is_step_done(slaposSupportTree)` / `is_step_done(slaposPad)` — with zero awareness of the gizmo's own `m_volumes`. Those same two flags also drive `SLAPrintObject::get_current_elevation()` (`SLAPrint.cpp:1228`): elevation stays applied as long as `is_step_done(slaposPad)` is still true, which is exactly why the model was floating — one stale flag, both symptoms. Since the early-return skipped `reslice_until_step()`, `SLAPrint::apply()` was never invoked, so those flags were never invalidated.

**This fix (removing the early `return` so both branches call `reslice_until_step()`) was necessary but turned out not sufficient** — re-test after this change showed the exact same symptoms, unchanged. Root-caused properly via temporary diagnostic logging — see 9f.

Fixed independently of 9f, and still correct: `GLGizmoHollow::resync_after_undo_redo()` now requests `slaposDrillHoles`, matching every other trigger point in the file.

### 9f. Real root cause of 9e's persisting symptoms, found via temporary diagnostic logging (2026-08-13), now fixed

**Fixed and confirmed by user (2026-08-13): single-mode undo/redo (Support alone, Hollow alone, Drill alone) no longer shows the pad-residual/floating-model/redo-not-regrowing symptoms, and no crashes observed. Temporary diagnostic logging removed.**

After 9e's fix still failed identically, added temporary `BOOST_LOG_TRIVIAL(info)` logging (tag `[UndoRedoSupportDebug]`) at every stage of the pipeline: `SLAPrint::apply()`'s model-identity/config-diff branches, `Plater::priv::update_after_undo_redo()` before/after the gizmos-manager call, `GLGizmosManager::update_after_undo_redo()`'s branch conditions, `GLGizmoSlaSupports::resync_after_undo_redo()`'s decision variables, `GLGizmoSlaBase::reslice_until_step()`'s deferred `CallAfter` firing, and `GLCanvas3D::reload_scene()`'s per-object step-done state and stale-GLVolume keep/delete decisions.

The log proved undo's own internal state was actually correct (config diff detected, `is_step_done(Pad)` correctly turns `false`, stale pad/support GLVolumes correctly marked for deletion). **Redo was the real, reproducible failure**: `is_step_done(Pad)` never turned `true` again, no matter how long the app was left running. The log traced exactly why:

`GLGizmosManager::update_after_undo_redo()` calls Support's, Hollow's, and Drill's `resync_after_undo_redo()` unconditionally, and (before this fix) each one independently called `reslice_until_step()` → `Plater::reslice_SLA_until_step()` → `restart_background_process(FORCE_RESTART)`, which unconditionally stops whatever the single shared background process is currently doing and restarts it. All three are deferred via `wxGetApp().CallAfter()` and fire back-to-back in the same idle-event batch — the log showed Support's restart begin computing supports ("16.5% Slicing model"), then get cancelled by Hollow's resync firing (`"got cancelled exception"`), restart, then get cancelled again by Drill's resync.

Crucially, this is worse than an ordinary cancel-and-retry, and **fully deterministic, not a timing fluke**: this fork never defines the `SUPPORT_BACKGROUND_PROCESSING` macro anywhere (confirmed via repo-wide grep, including CMake), so `Plater::priv::background_processing_enabled()` is hardcoded to always return `false`. Every `reslice_SLA_until_step()` call therefore always sets `task.to_object_step = <the calling gizmo's own target>`, capping how far that restart is allowed to recompute. Since `GLGizmosManager` always calls Support → Hollow → Drill in that fixed order, and Hollow/Drill's target (`slaposDrillHoles`) sits *earlier* in the SLA pipeline than Support's (`slaposSupportPoints`/`slaposPad`), **the last one queued — Drill's — always wins the race** and permanently caps the recompute short of ever reaching supports/pad, regardless of what Support actually needed. This reproduces on *every* undo/redo landing on an SLA object with anything for Support to recompute, whether or not that object has any actual hollow/drill data (Hollow/Drill's resyncs run, and cap the pipeline, unconditionally either way).

**Fix**: `resync_after_undo_redo()` on all three gizmos no longer calls `reslice_until_step()` itself — each now only does its own local, non-backend-touching sync and *returns* the `SLAPrintObjectStep` it needs recomputed (`GLGizmoHollow`/`GLGizmoDrill` always return `slaposDrillHoles`; `GLGizmoSlaSupports` returns `slaposPad`/`slaposSupportPoints` as before, per `has_support_data`/`m_show_support_structure`). `GLGizmosManager::update_after_undo_redo()` collects all three returned steps, takes the deepest via `std::max()`, and issues exactly **one** `reslice_until_step()` call for it — so there is only ever a single background-process restart per undo/redo, and it always covers the deepest step actually needed across all three domains. `slaposDrillHoles` is always safely subsumed by `max()` since it's strictly earlier in the pipeline than any support step, so folding it in unconditionally (even with no real hollow/drill data) never trims what Support needs.

Temporary diagnostic logging (`[UndoRedoSupportDebug]`) has been removed from `SLAPrint.cpp`, `Plater.cpp`, `GLCanvas3D.cpp`, and `GLGizmoSlaBase.cpp` now that its job is done — those four files are back to their pre-investigation state, save for this fix's actual code changes in `GLGizmoSlaSupports`/`GLGizmoHollow`/`GLGizmoDrill`/`GLGizmosManager`.

**Scope decision (user, 2026-08-13): stop here for this change.** While reasoning through this fix, a *separate, plausible* gap was identified — a multi-mode sequence like Support Apply (Auto) → Hollow Apply → Support Apply again (Auto) → undo may not correctly recompute the support geometry, because `SLAPrintObject::config().diff()`'s point-invalidation logic (`SLAPrint.cpp:604-613`) can't detect a point-coordinate difference between two Auto-mode Applies (it only checks point equality when `sla_points_status == UserModified`). This has **not** been reproduced or tested — it's a code-reading-derived hypothesis, not a confirmed bug. Per the user's explicit direction, this is deliberately left out of scope for this change; a future, separate spec should investigate and fix it if it's ever confirmed to matter in practice. See design.md's "Open Questions" section for the full note.

- [x] 10.1 Confirmed by code review: non-mode undo/redo machinery (`Plater::priv::undo/redo/undo_redo_to`, `UndoRedo::Stack`, FDM gizmos, Mechanism A for FDM painting gizmos) not modified by the Decision G pivot.
- [x] 10.2 Structural-mutation entry-point audit (Section 5) — superseded, not needed under single-stack. The one requirement it partially covered that still matters — a gizmo not crashing when its focused object vanishes — is fully covered by the retained Decision E.3 null-guard, verified per-gizmo in Section 9.7.
- [x] 10.3 Accepted residuals carried forward from the abandoned design, still relevant:
  - Undo history is intentionally verbose under single-stack (Decision G accepted trade-off) — not a residual bug, a deliberate choice.
- [ ] 10.4 Once Section 9 passes, archive this change; sync `resin-mode-single-stack-undo-redo` and the trimmed `resin-mode-structural-mutation-safety` into the main specs; do **not** sync the abandoned `resin-mode-scoped-undo-stack` spec (already removed from `specs/`).
