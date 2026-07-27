## ADDED Requirements

### Requirement: Entering a resin edit mode opens a scoped undo/redo sub-stack

When the user enters any of the three resin edit modes — Generate support (SlaSupports editing), Hollow, or Drill — the system SHALL open a scoped secondary undo/redo sub-stack anchored at a baseline snapshot representing the exact state at mode entry. While the mode is active, undo/redo operations SHALL act on this sub-stack, not on the main plater undo stack.

Mode-entry trigger points differ per gizmo and are preserved: Hollow and Drill enter on gizmo open (`on_set_state` On); SlaSupports enters on its Manual editing mode. All three SHALL route through a single shared enter/leave mechanism.

#### Scenario: Opening Hollow anchors a baseline

- **WHEN** the user opens the Hollow mode
- **THEN** a scoped sub-stack SHALL be created with a baseline snapshot capturing the model state at the moment Hollow was opened

#### Scenario: Opening Drill anchors a baseline

- **WHEN** the user opens the Drill mode
- **THEN** a scoped sub-stack SHALL be created with a baseline snapshot capturing the model state at the moment Drill was opened

#### Scenario: Entering SlaSupports Manual editing anchors a baseline

- **WHEN** the user enters SlaSupports Manual editing mode
- **THEN** a scoped sub-stack SHALL be created with a baseline snapshot capturing the model state at editing-mode entry

---

### Requirement: The in-mode undo/redo step is the Apply commit, not the pending UI edit

Within a resin edit mode, a sub-stack snapshot ("in-mode step") SHALL be created only when the mode commits its pending state to the model — i.e. an Apply-class action (Drill "Apply drain holes"; Hollow "Hollow" button; SlaSupports Manual Apply / point commit). Transient pending UI operations that do not write to the model — parameter sliders/inputs (Hollow thickness/quality/closing; Drill hole diameter/depth) and Drill hole Add/Delete/Move/Size on the working set — SHALL NOT each create a sub-stack snapshot. This preserves the existing pending-apply model of these gizmos.

#### Scenario: Pending parameter edits create no in-mode step

- **WHEN** the user drags parameter sliders or adjusts hole size/placement inside a mode without committing (no Apply)
- **THEN** no sub-stack snapshot SHALL be created for those pending operations

#### Scenario: Each Apply creates exactly one in-mode step

- **WHEN** the user commits pending state via an Apply-class action inside a mode
- **THEN** exactly one snapshot SHALL be added to the scoped sub-stack representing the committed model state

#### Scenario: Multiple Applies in one session create independent in-mode steps

- **WHEN** the user performs Apply twice within a single mode session
- **THEN** the sub-stack SHALL contain two independent in-mode steps between which in-mode undo/redo can navigate

---

### Requirement: In-mode undo is bounded by the mode-entry baseline

While a resin edit mode is active, repeated undo SHALL NOT restore any state older than the mode-entry baseline. Undo at the baseline SHALL be a safe no-op and SHALL NOT dereference an invalid iterator or fall through to the main stack. Because only Apply commits create in-mode steps, in-mode undo navigates between Apply boundaries (and back to the baseline), not between individual pending edits.

#### Scenario: Undo stops at baseline in Drill

- **WHEN** the user performs one or more Applies inside Drill, then presses Ctrl+Z more times than there were Applies
- **THEN** undo SHALL restore back through each Apply boundary to the mode-entry baseline and stop there
- **THEN** undo SHALL NOT restore any state that existed before Drill was opened
- **THEN** the application SHALL NOT crash

#### Scenario: Undo stops at baseline in Hollow

- **WHEN** the user performs one or more Hollow Applies, then presses Ctrl+Z more times than there were Applies
- **THEN** undo SHALL restore back through each Apply boundary to the mode-entry baseline and stop there
- **THEN** undo SHALL NOT restore any state that existed before Hollow was opened
- **THEN** the application SHALL NOT crash

---

### Requirement: In-mode redo is bounded to Apply steps created within the current mode

While a resin edit mode is active, redo SHALL only advance across Apply steps created within the current mode session. Redo SHALL NOT advance into snapshots from before the mode was entered or into unrelated main-stack history.

#### Scenario: Redo only replays in-mode Apply steps

- **WHEN** the user performs Applies inside a mode, undoes some of them, then presses Ctrl+Y (redo)
- **THEN** redo SHALL re-apply only the Apply steps created inside the current mode session
- **THEN** redo SHALL NOT advance into any state outside the current mode session

---

### Requirement: Pending working state re-initializes from the model after in-mode undo/redo

After an in-mode undo/redo restores the model to a prior Apply boundary (or the baseline), each gizmo SHALL rebuild its pending working state (Drill `m_working_holes`; Hollow pending parameter values) from the restored model so that rendering, raycasters, and control values reflect the restored state. Gizmos SHALL rely on the existing serialization/refresh path for this (e.g. Drill `data_changed(is_serializing=true)` rebuilding `m_working_holes`; Hollow `on_load` resetting its pending owner so parameters re-init from the restored config).

#### Scenario: In-mode undo refreshes Drill working set

- **WHEN** the user undoes an Apply while Drill is active
- **THEN** `m_working_holes` SHALL be rebuilt from the restored `mo->sla_drain_holes` and the rendered holes SHALL match the restored state

#### Scenario: In-mode undo refreshes Hollow parameters

- **WHEN** the user undoes a Hollow Apply while Hollow is active
- **THEN** the pending parameter controls SHALL re-initialize from the restored `mo->config`

---

### Requirement: Leaving a mode collapses the session into a single main-stack snapshot

When the user leaves a resin edit mode after having committed at least one Apply that produced a net change relative to the baseline, the system SHALL record exactly one snapshot on the main plater undo stack representing the final committed result, regardless of how many Applies were performed inside the mode. A single subsequent Ctrl+Z on the main stack SHALL restore the state that existed before the mode was entered.

#### Scenario: Multiple Drill Applies collapse to one main snapshot

- **WHEN** the user opens Drill, performs Apply twice, then leaves Drill
- **THEN** the main plater undo stack SHALL gain exactly one snapshot representing the final drain-hole result
- **THEN** a single Ctrl+Z after leaving SHALL restore the state that existed before Drill was opened

#### Scenario: Multiple Hollow Applies collapse to one main snapshot

- **WHEN** the user opens Hollow, presses the Hollow button multiple times with different parameters, then leaves Hollow
- **THEN** the main plater undo stack SHALL gain exactly one snapshot representing the final hollowing result
- **THEN** a single Ctrl+Z after leaving SHALL restore the state that existed before Hollow was opened

---

### Requirement: No main-stack snapshot for a no-op mode session

When the user leaves a resin edit mode without a committed net change — either no Apply was performed, or Applies were performed but undone back to the mode-entry baseline — the system SHALL NOT record any snapshot on the main plater undo stack. The no-op decision SHALL be made structurally, based on whether the sub-stack holds an undoable snapshot relative to the baseline at the moment of leaving.

#### Scenario: Enter and leave without Apply records nothing

- **WHEN** the user opens a mode, optionally adjusts pending parameters/holes, but leaves without pressing Apply
- **THEN** the main plater undo stack SHALL be unchanged (no new snapshot)

#### Scenario: Applies fully undone before leaving records nothing

- **WHEN** the user opens a mode, performs Applies, undoes back to the mode-entry baseline, then leaves
- **THEN** the main plater undo stack SHALL be unchanged (no new snapshot)

---

### Requirement: Non-mode undo/redo behavior is unchanged

Undo/redo of operations performed on the main plater stack outside any resin edit mode SHALL behave exactly as before this change.

#### Scenario: Main-stack operations still undo normally

- **WHEN** the user performs ordinary main-stack operations (move, rotate, scale, delete) outside any resin edit mode and then undoes them
- **THEN** each operation SHALL undo/redo one step at a time exactly as it did prior to this change
