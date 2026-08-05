# Tasks

> This change is planning-only for now (proposal + design + tasks). Do not start section 2+ implementation until the user explicitly asks to proceed — see conversation context: they chose "report only, don't fix in this branch yet".

## 0. Preconditions / evidence capture

- [x] 0.1 Reproduce and log-verify the bug: confirm via `debug_*.log.0` that `can_undo`/`can_redo` flip `true` with zero intervening `take_snapshot()` calls, purely from an `IsShown()`-gating change (already done — see design.md Context)
- [x] 0.2 Trace `has_undo_snapshot()`/`has_redo_snapshot()` vs. `undo()`/`redo()`'s `SIZE_MAX` resolution and confirm they disagree at the documented boundary (already done — see design.md Decision A)
- [ ] 0.3 Confirm current call sites of `has_undo_snapshot()` / `has_redo_snapshot()` are exactly `Plater::can_undo()` / `Plater::can_redo()` and nothing else depends on their (buggy) return value for state mutation: `grep -rn "has_undo_snapshot()\|has_redo_snapshot()" src/`

## 1. Decision B audit — sibling functions

- [ ] 1.1 `has_undo_snapshot(size_t time_to_load)` — determine live callers (`grep -rn "has_undo_snapshot("`), confirm whether its `time_to_load < m_active_snapshot_time && binary_search(...)` shape has the same topmost-placeholder misclassification; record disposition
- [ ] 1.2 `has_real_change_from(time)` (dirty-state check, ~UndoRedo.cpp:1299-1320) — same audit
- [ ] 1.3 `release_snapshots()`'s `it_saved` walk (~UndoRedo.cpp:1150-1160) — confirm it does not misclassify the topmost placeholder as "saved"; record disposition
- [ ] 1.4 Sweep the remaining `m_active_snapshot_time`-relative `lower_bound`/scan call sites not already covered by design.md's table (re-run `grep -n "m_active_snapshot_time\|std::lower_bound\|is_topmost" src/slic3r/Utils/UndoRedo.cpp` and check off each one)
- [ ] 1.5 Summarize the audit as a disposition table (fix-needed vs. confirmed-fine) and get it reviewed before starting section 2, since anything beyond `has_undo_snapshot()`/`has_redo_snapshot()` is scope creep that needs an explicit in/out decision first

## 2. Fix `has_undo_snapshot()` (Decision A, Option 1)

- [ ] 2.1 Change the boundary so the origin entry (`m_snapshots.begin()`) is excluded once the active position resolves down to being adjacent to it, mirroring `undo()`'s `SIZE_MAX` branch (`--it_current == m_snapshots.begin() → false`)
- [ ] 2.2 Verify the fix still correctly returns `true` when there are real modifying entries between `begin()` and `active` beyond just the adjacent one (e.g. `[New Project, Add Primitive, Move Object, topmost]` → `has_undo_snapshot()` true at topmost)
- [ ] 2.3 Verify silent/non-modifying entries (name ending in `!`, e.g. `"select partplate!"`) between `begin()` and `active` are still correctly skipped over (not counted, but also don't block finding a real entry further back)

## 3. Fix `has_redo_snapshot()` (Decision A, Option 1)

- [ ] 3.1 Change the scan to start strictly after `active` (mirroring `redo()`'s `++it_current`), excluding the active entry itself from consideration
- [ ] 3.2 Verify the fix still correctly returns `true` when the redo target is not immediately adjacent (e.g. after two undos, `has_redo_snapshot()` true, and correctly identifies there's a real entry to redo to, skipping any silent entries)

## 4. Tests — Layer 1 (automated, spike first)

- [ ] 4.1 Spike: determine whether the corrected boundary logic can be extracted into a small, pure helper operating only on `std::vector<Snapshot>` + timestamp (no `Slic3r::Model`/`Selection`/`GLGizmosManager`/`PartPlateList` dependency), such that it's linkable into `libslic3r_tests`. If yes, extract it and use it from both `has_undo_snapshot()`/`has_redo_snapshot()` and the new test.
- [ ] 4.2 If the spike succeeds: add `tests/libslic3r/test_undo_redo_availability.cpp` (or extend an existing file) covering the scenarios in `specs/undo-redo-availability-query/spec.md` — fresh project, after 1/N actions, after undo-to-origin, after redo-to-top, silent-entry skipping
- [ ] 4.3 If the spike fails (extraction isn't cheap without a larger refactor): document why in this file, and rely on the Layer 2 manual matrix as primary verification for this change — consistent with `resin-mode-scoped-undo-redo`'s precedent for `UndoRedo::StackImpl`-adjacent GUI-coupled logic
- [ ] 4.4 Run the full existing `tests/libslic3r` suite (not just any new test) to confirm no regression, once a test build is available

## 5. Tests — Layer 2 (manual matrix, GUI)

- [ ] 5.1 Fresh project (nothing done): topbar Undo/Redo AND Edit menu Undo/Redo all show disabled
- [ ] 5.2 After one project-modifying action (e.g. add a primitive): Undo enabled, Redo disabled, in both the topbar and Edit menu
- [ ] 5.3 After undoing back to the very start: Undo disabled, Redo enabled, in both the topbar and Edit menu (this is the exact scenario that was empirically broken — re-run the original repro: load model → move → undo × 2 → confirm Undo is now correctly disabled instead of staying enabled)
- [ ] 5.4 After redoing back to the top: Undo enabled, Redo disabled
- [ ] 5.5 Multiple actions with an undo/redo walk in the middle (not just all-the-way-back/forward): availability toggles correctly at every step, not just the two extremes
- [ ] 5.6 A session with a silent (`!`-suffixed) entry in the middle of real history (e.g. a plate-select in between two real edits) does not throw off availability at either boundary
- [ ] 5.7 Re-verify this doesn't regress the resin-mode scoped sub-stack behavior from `resin-mode-scoped-undo-redo` (Generate support / Hollow / Drill in-mode undo/redo bounded to baseline) — since `has_undo_snapshot()`/`has_redo_snapshot()` are called on whichever stack is currently active (main or gizmos sub-stack), this fix applies to both without special-casing, but needs empirical confirmation

## 6. Wrap-up

- [ ] 6.1 Confirm via code review that only `has_undo_snapshot()`/`has_redo_snapshot()` (plus anything section 1's audit flagged as needing a fix) changed — `take_snapshot()`, `undo()`, `redo()`, and serialization are untouched
- [ ] 6.2 Cross-link back to `fix/topbar-undo-redo-state` in the commit message/PR description as "surfaced by, but independent of" that change
- [ ] 6.3 Compile verification (per project convention: do not run the build yourself — hand off to the user with "程式碼修改完成，請執行編譯驗證。")
