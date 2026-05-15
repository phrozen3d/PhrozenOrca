# Capability: sla-supports-apply-undo-stack

## Purpose

Defines the correct undo/redo behaviour for the SlaSupports Manual Apply flow. When the user clicks **Apply** inside the SlaSupports Manual editing session, the resulting snapshot must be recorded in the main plater undo stack so that subsequent Ctrl+Z / UI Undo operations restore support points reliably and without crashes.

## Requirements

### Requirement: Manual Apply snapshot recorded in main undo stack

When the user clicks the **Apply** button inside the SlaSupports Manual editing session, the resulting `"Support points edit"` snapshot SHALL be recorded in the main plater undo stack, not in the gizmo-local undo stack.

#### Scenario: Manual Apply followed by gizmo switch does not lose undo entry

- **WHEN** the user places support points, clicks **Apply** in Manual mode, then switches to a different gizmo (e.g., Hollow or Move)
- **THEN** the main plater undo stack SHALL contain a `"Support points edit"` snapshot representing the applied state

#### Scenario: One Ctrl+Z after Apply and gizmo switch restores to pre-apply state only

- **WHEN** the user places support points, clicks **Apply**, switches to another gizmo, then presses Ctrl+Z once
- **THEN** `mo->sla_support_points` SHALL be restored to the state immediately before that Apply click
- **THEN** the restored state SHALL NOT jump back to the state that existed before the SlaSupports editing session was opened

#### Scenario: UI Undo button after Apply and gizmo switch behaves identically to Ctrl+Z

- **WHEN** the user places support points, clicks **Apply**, switches to another gizmo, then clicks the UI Undo toolbar button once
- **THEN** `mo->sla_support_points` SHALL be restored to the state immediately before that Apply click
- **THEN** the restored state SHALL NOT jump back to the state before the SlaSupports editing session was opened
- **THEN** the application SHALL NOT crash

#### Scenario: Multiple Ctrl+Z after multiple Manual Applies navigates each Apply independently

- **WHEN** the user performs Apply once (3 points), continues editing, performs a second Apply (5 points), switches gizmo, then presses Ctrl+Z twice
- **THEN** the first Ctrl+Z SHALL restore `mo->sla_support_points` to the 3-point state (after the first Apply)
- **THEN** the second Ctrl+Z SHALL restore `mo->sla_support_points` to the 0-point state (before the first Apply)

---

### Requirement: Gizmo editing session continues with fresh baseline after Manual Apply

After a Manual Apply snapshot is committed to the main stack, the SlaSupports editing session SHALL remain active (UI does not close, editing mode is not exited) with a new gizmo-stack baseline anchored at the applied state.

#### Scenario: Editing mode stays active after Apply

- **WHEN** the user clicks **Apply** in Manual mode
- **THEN** the editing session SHALL remain open (the Manual editing panel SHALL still be visible)
- **THEN** `m_editing_mode` SHALL remain `true`

> **Out of scope — In-session undo routing inside Manual Editing mode**: Undo/redo behavior while the user remains inside the SlaSupports Manual Editing session (`m_editing_mode == true`) operates on the gizmo-local undo stack and is outside this capability's requirements. This includes whether in-session Ctrl+Z after Apply reverts only newly added points. See [KB-4] and candidate change `fix-sla-supports-active-undo-routing`.

---

### Requirement: Point-sphere display reflects restored model state in the core Manual Apply restore flow

After a Manual Apply followed by leaving SlaSupports or switching gizmo, undo/redo SHALL restore both `mo->sla_support_points` and the SlaSupports point-sphere display. Both Ctrl+Z/Ctrl+Y keyboard shortcuts and UI Undo/Redo toolbar buttons SHALL produce consistent results in this core flow.

> **Out of scope — Active SlaSupports undo routing**: Whether undo/redo immediately refreshes the point-sphere display while SlaSupports remains the active gizmo (Auto mode or Manual Editing mode) depends on active undo routing behaviour tracked by [KB-4]. See candidate change `fix-sla-supports-active-undo-routing`. This requirement only covers the core post-leave / post-gizmo-switch restore flow.

#### Scenario: After leaving SlaSupports, undo then re-open shows correct point spheres

- **WHEN** the user performs a Manual Apply, leaves SlaSupports (switches to another gizmo), then presses Ctrl+Z or clicks the UI Undo toolbar button
- **THEN** when the user subsequently re-opens SlaSupports, the point sphere display SHALL match the restored `mo->sla_support_points`
- **THEN** the application SHALL NOT crash

#### Scenario: UI Redo button after undoing a Manual Apply restores support points

- **WHEN** the user has undone a Manual Apply (via Ctrl+Z or UI Undo button), then clicks the UI Redo toolbar button
- **THEN** `mo->sla_support_points` SHALL be restored to the applied state
- **THEN** the application SHALL NOT crash

---

### Requirement: Reslice after undo does not overwrite UserModified support points

When an undo or redo triggers `reslice_until_step(slaposSupportPoints)` and `mo->sla_points_status` is `UserModified`, the slicing pipeline SHALL use `mo->sla_support_points` directly and SHALL NOT run auto-generation to produce a new point set.

#### Scenario: Undo to UserModified snapshot preserves user points through reslice

- **WHEN** a snapshot with `sla_points_status == UserModified` is restored by undo
- **WHEN** `reslice_until_step(slaposSupportPoints)` is subsequently triggered
- **THEN** the slicing pipeline SHALL use the restored `mo->sla_support_points` as-is
- **THEN** the pipeline SHALL NOT overwrite `mo->sla_support_points` with auto-generated points

#### Scenario: Undo to Generating snapshot re-runs auto-generation

- **WHEN** a snapshot with `sla_points_status == Generating` is restored by undo
- **WHEN** `reslice_until_step(slaposSupportPoints)` is triggered
- **THEN** the pipeline SHALL re-run auto-generation and update `mo->sla_support_points` with the generated result

> **Out of scope — Empty Manual Apply semantics**: This capability does not define the behaviour of a Manual Apply that commits zero support points (`UserModified + empty list`). Undo/redo SHALL restore this state consistently (same as any other Manual Apply result), but whether slicing should skip auto-generation or reset to auto-generate is a product decision outside this spec. See candidate change `fix-sla-supports-empty-apply-semantics`.

> **Out of scope — Structure display mode refresh**: When SlaSupports is in Structure view mode, undo/redo display refresh of the support mesh (tree + pad) depends on async reslice completion and is outside this capability's requirements. This spec only covers `m_normal_cache` / support point sphere display consistency.

> **Out of scope — Post-Apply UI mode**: The spec requires both `editing_mode_apply_changes()` (Enter key / gizmo deactivation) and the Manual Apply button to preserve support data correctly in the undo stack. It does not require both paths to leave the user in the same UI mode after Apply.

---

### Requirement: Undo and redo operations are crash-safe within the SlaSupports Manual Apply context

Repeated undo/redo operations related to SlaSupports Manual Apply SHALL NOT crash, regardless of the entry point (keyboard shortcut or UI toolbar button), and SHALL perform a safe no-op when reaching the stack boundary rather than dereferencing an invalid iterator.

#### Scenario: Repeated undo via Ctrl+Z after Manual Apply reaches boundary without crashing

- **WHEN** the user performs a Manual Apply, leaves SlaSupports, then presses Ctrl+Z repeatedly until no further undo is available
- **THEN** each Ctrl+Z SHALL either restore a valid prior snapshot or perform a safe no-op
- **THEN** the application SHALL NOT crash at any point during the repeated undo sequence

#### Scenario: Repeated undo via UI Undo button after Manual Apply reaches boundary without crashing

- **WHEN** the user performs a Manual Apply, leaves SlaSupports, then clicks the UI Undo toolbar button repeatedly until no further undo is available
- **THEN** each click SHALL either restore a valid prior snapshot or perform a safe no-op
- **THEN** the application SHALL NOT crash at any point during the repeated undo sequence
