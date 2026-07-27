## Context

The codebase already contains two undo/redo mechanisms:

- **Mechanism A** — `wants_enter_leave_snapshots()` + `GLGizmosManager::activate_gizmo`: inserts `EnteringGizmo` / `GizmoAction` / `LeavingGizmo*` markers on a single main stack, with `reduce_noisy_snapshots()` merging consecutive same-named `GizmoAction`s. Used by FDM painting gizmos.
- **Mechanism B** — `Plater::enter_gizmos_stack()` / `leave_gizmos_stack()`: two physical stacks (`m_undo_redo_stack_main` + `m_undo_redo_stack_gizmos`) with an active pointer `m_undo_redo_stack_active`. Entering pushes a `"Gizmos-Initial"` baseline onto the gizmos stack; leaving clears it and switches back to main. This is the genuine scoped sub-stack.

Mechanism B already satisfies the desired "enter mode → scoped sub-stack, bounded undo/redo, collapse-to-one on leave" behavior. It is used today by **SlaSupports** (`switch_to_editing_mode` / `disable_editing_mode` / `commit_manual_edits_keep_editing`) and by the FDM-only **BrimEars** (`on_set_state`). **Hollow and Drill do not use it at all** (`wants_enter_leave_snapshots()` is the base default `false`); each Apply takes a flat snapshot directly on main.

**Hollow and Drill are "apply-only" (pending-apply) today** (verified in code + landed specs `drill-apply-only-undo`, `hollow-action-buttons`): pending UI edits — Hollow parameter sliders → `m_pending_*`; Drill hole Add/Delete/Move/Size → `m_working_holes` — do **not** snapshot. Only the Apply button commits pending → model with one `TakeSnapshot` on main. This change **extends** that model rather than replacing it: the in-mode undo step stays at Apply granularity (定義 X), and we only re-route the Apply snapshot onto a scoped sub-stack and add the leave-collapse. Note the `drill-apply-only-undo` design here superseded an earlier `fix-sla-undo-redo` proposal (`begin_size_change`/`apply_size_change`), which was reverted (`040a240eee`) — so this change builds on `drill-apply-only-undo`, not on that dead design.

Key current facts (verified in code):

- `activate_gizmo` turns the old gizmo `Off` before turning the new one `On`, so a direct gizmo→gizmo switch fires `on_set_state(Off)` then `on_set_state(On)`.
- `Plater::TakeSnapshot` records onto `m_undo_redo_stack_active` — so a snapshot taken while a sub-stack is active lands on the sub-stack.
- Deleting the focused object while a mode is active is unguarded: `Plater::remove_selected()` takes its `"Delete Selected Objects"` snapshot on the active stack (possibly the sub-stack) and then `Selection::erase()` frees the ModelObject without resetting the gizmo — the gizmo keeps a dangling `m_c` pointer.
- `can_delete()` is **not** the only delete gate: `ObjectList::remove()` (bound to `wxID_DELETE`) deletes directly and bypasses `can_delete()`.

Prerequisite state is already landed in code: Drill's `data_changed(is_serializing=true)` rebuild of `m_working_holes` (from `drill-apply-only-undo`) and Hollow's real `on_save/on_load` serialization + `m_pending_owner = nullptr` reset (the Hollow half of `fix-sla-undo-redo`, present in source). These provide the pending-working-state re-init needed after in-mode undo. This change does **not** depend on the reverted Drill `begin_size_change`/`apply_size_change` design.

## Goals / Non-Goals

**Goals:**

- Bring Hollow and Drill onto Mechanism B (scoped sub-stack) with bounded in-mode undo/redo, collapse-to-one on leave, and no-op skip.
- Unify the enter/leave sub-stack logic into one shared mechanism used by all three gizmos, so behavior and future maintenance live in one place.
- Guarantee crash-safety when a structural mutation frees the focused ModelObject mid-mode, via a containment invariant plus a gizmo-level fallback that needs no entry-point enumeration.
- Keep non-mode undo/redo behavior unchanged.

**Non-Goals:**

- No "prevention" approach (disabling delete / delete-all while in a mode). It has the `wxID_DELETE` bypass and cannot cover non-delete structural mutations; it is at most optional UX polish and is excluded from this change.
- No change to the `UndoRedo::Stack` core data structures or Model serialization format.
- No change to FDM gizmos or Mechanism A.
- No content-level "net diff vs baseline" no-op detection (structural detection only; see Decision C).

## Decisions

### Decision A — Shared entry via two explicit `GLGizmoBase` methods (not a RAII scope)

Add `enter_mode_undo_stack()` / `leave_mode_undo_stack()` (naming aligned with the existing `enter_gizmos_stack`) as shared methods reachable by all three gizmos. `leave` internally computes the structural "changed?" result and takes at most one main snapshot.

**Why not RAII scope (`GizmoModeUndoScope`):** SlaSupports' `commit_manual_edits_keep_editing` performs `leave → snapshot → apply → re-enter` mid-session (commit while staying in editing mode). That lifecycle does not fit a pure ctor/dtor scope. Explicit methods accommodate all three usages: Hollow/Drill (open/close) and SlaSupports (enter editing / commit-and-re-enter / final leave). The pairing-omission risk (early-return skipping leave) is covered by the idempotency + fallback in Decision E.

### Decision B — Leave-time main snapshot name routed through a single per-gizmo virtual

The name of the collapsed main-stack snapshot is provided by a per-gizmo virtual (aligned with the existing `get_action_snapshot_name()` / `get_gizmo_leaving_text()` family) rather than hard-coded at each call site. Keeps names discoverable, translatable, and gives a template for a future fourth mode. Current names preserved: SlaSupports `"Support points edit"`, Hollow `"Hollow"`, Drill `"Apply drain holes"`.

### Decision F — In-mode step granularity is per-Apply, not per-UI-edit (定義 X)

An in-mode sub-stack step is created only on an Apply-class commit (Drill "Apply drain holes"; Hollow "Hollow" button; SlaSupports Manual Apply). Pending UI operations (Hollow parameter sliders; Drill hole Add/Delete/Move/Size on `m_working_holes`) stay pending and do not snapshot.

**Why not per-UI-edit (定義 Y):** undo units are model states; pending edits have not yet touched the model/preview, so undoing an individual pending slider is semantically odd. Per-Apply also preserves the landed pending-apply architecture (`m_working_holes` / `m_pending_*`) — this change becomes an *extension* (re-route Apply snapshot to the sub-stack + leave-collapse) rather than a rewrite, and avoids conflicting with `drill-apply-only-undo`. It still fully satisfies the original requirements (bounded in-mode undo/redo across Apply boundaries, collapse-to-one on leave, no-op skip).

**Consequence — pending re-init:** after in-mode undo restores an earlier Apply/baseline, each gizmo must rebuild its pending working state from the restored model (Drill `data_changed(is_serializing=true)` → `m_working_holes`; Hollow `on_load` reset → parameters re-init from config). This infra already exists and is reused, not rebuilt.

### Decision C — Structural "changed?" detection, not content diff

The no-op decision uses the structural signal already returned by `leave_gizmos_stack()` (`has_undo_snapshot()` — is there an undoable snapshot relative to the baseline). This covers both required no-op cases: no edits (only baseline present) and edits-undone-to-baseline (active == baseline).

**Accepted residual:** editing a value and then manually setting it back to the baseline value leaves `active != baseline` with identical content, so a redundant snapshot would be recorded. A content compare against the baseline would eliminate this edge but adds cost and complexity; deferred as a nice-to-have to avoid over-engineering v1.

### Decision D — Per-gizmo entry trigger preserved; mechanism unified

Unify the *mechanism* but keep each gizmo's *entry trigger*:

- **Hollow / Drill:** hook `on_set_state` — On → `enter_mode_undo_stack()`; Off → `leave_mode_undo_stack()`. Keep the existing Apply-time `TakeSnapshot("Hollow")` / `TakeSnapshot("Apply drain holes")` but ensure it lands on the active sub-stack (it will, because the sub-stack is active between On/Off); do **not** add per-UI-edit snapshots (Decision F). The multiple in-session Apply snapshots collapse to one main snapshot at leave.
- **SlaSupports:** keep `switch_to_editing_mode` / `disable_editing_mode` / `commit_manual_edits_keep_editing` as entry points, refactored to call the shared methods. Its observable Manual Apply behavior (per `sla-supports-apply-undo-stack`) is unchanged.

BrimEars is FDM-only and out of scope; it is a code-pattern reference only. The correct ordering is **leave → then main `TakeSnapshot`** (as SlaSupports' commit does), so the collapsed snapshot lands on main; BrimEars' `snapshot → update → leave` ordering is a reference counter-example, not to be copied.

### Decision E — Containment invariant + idempotent enter/leave + gizmo null-guard fallback

Three layers, chosen over "prevention" because they cover all structural entry points (including future ones) without enumeration:

1. **Choke point:** at the structural-mutation entry path, before the mutation's snapshot is taken on main, force-collapse an active sub-stack (`reset_all_states` / disable editing → triggers the gizmo's leave → switch active back to main). Applied at the delete/load/reload/plate-clear paths, and covering the `wxID_DELETE` (`ObjectList::remove`) bypass.
2. **Idempotent enter/leave:** `leave` is a no-op when already on main; `enter` collapses an existing session first instead of asserting. This makes direct mode→mode switches and any missed path safe.
3. **Fallback null-guard:** each gizmo detects a vanished focused ModelObject (empty selection / null model object) in its data-refresh / render / leave paths and self-closes without dereferencing; the leave path treats a vanished object as a no-op.

### Decision — Structural-mutation entry-point audit

To implement the choke point reliably, enumerate and check off the structural entry points (containment applied at each, or provably covered by the shared choke): `Plater::remove_selected`, `Plater::priv::delete_object_from_model`, `Plater::priv::delete_all_objects_from_model`, `Plater::priv::remove_curr_plate_all`, `ObjectList::remove` (`wxID_DELETE`), instance removal / decrease-instances, New/Open/import project, reload-from-disk / replace-with-mesh. The audit table lives in tasks.md.

## Risks / Trade-offs

- **[Refactoring working SlaSupports into the shared mechanism risks regressing its Manual Apply undo behavior]** → The shared `leave` must support the commit-and-re-enter flow (leave → snapshot → apply → re-enter). Cover with the existing `sla-supports-apply-undo-stack` scenarios as a regression gate before and after refactor.
- **[Missed structural entry point strands a snapshot or dangles a pointer]** → The gizmo null-guard fallback (Decision E.3) is the entry-point-agnostic safety net; the choke point (E.1) is the primary fix, the audit table the coverage record.
- **[Catch2 cannot exercise the GUI enter/leave/no-op behavior]** → Two-layer test strategy: Layer 1 automated serialization round-trips at libslic3r level (extend `tests/libslic3r/test_sla_undo_redo_data.cpp` for Hollow params + drain holes); Layer 2 a documented manual matrix (6 requirements × 3 modes, plus the structural-mutation-while-in-mode cases). Layer 3 (extracting a pure `should_commit_on_leave` predicate for unit testing) is low ROI due to GUI dependencies of `Stack::take_snapshot` and is deferred.
- **[Extending two landed apply-only models]** → The landed `drill-apply-only-undo` and Hollow pending-apply behaviors must keep working; only the snapshot's stack destination and the leave-collapse change. Mitigate by re-verifying the `drill-apply-only-undo` scenarios (pending set, no-snapshot-for-intermediate-ops, data_changed rebuild, exit-discards) still hold after re-routing to the sub-stack; MODIFIED delta captures exactly what changed.
- **[Residual redundant snapshot on edit-then-revert-to-baseline-value]** (Decision C) → Accepted for v1; documented as a follow-up candidate.

## Open Questions

- Should the containment choke live in a single helper invoked at each structural entry, or be centralized behind the main-stack `TakeSnapshot` path (assert-and-collapse when a sub-stack is active while taking a structural snapshot)? Both satisfy the invariant; the latter is harder to bypass but touches a hotter path. To be settled during implementation of the choke point.
