## ADDED Requirements

### Requirement: GLGizmoDrill maintains m_working_holes as session pending set

GLGizmoDrill SHALL maintain a member `sla::DrainHoles m_working_holes` as the exclusive working set for the active Drill session. `m_working_holes` SHALL be initialized from `mo->sla_drain_holes` when a Drill session begins (gizmo activated or object switched), and SHALL be the sole data source for rendering, raycaster registration, drag operations, selection, and size changes during the session. `mo->sla_drain_holes` SHALL NOT be modified by any Add/Delete/Move/Size operation during the session.

#### Scenario: Add hole writes to m_working_holes only

- **WHEN** the user left-clicks to add a drain hole during a Drill session
- **THEN** the new hole SHALL be appended to `m_working_holes`
- **THEN** `mo->sla_drain_holes` SHALL remain unchanged
- **THEN** no undo snapshot SHALL be created

#### Scenario: Delete hole writes to m_working_holes only

- **WHEN** the user deletes selected holes via "Remove selected" or "Remove all"
- **THEN** the holes SHALL be removed from `m_working_holes`
- **THEN** `mo->sla_drain_holes` SHALL remain unchanged
- **THEN** no undo snapshot SHALL be created

#### Scenario: Drag (move) writes to m_working_holes only

- **WHEN** the user drags a hole to a new position and releases
- **THEN** the updated position/normal SHALL be written to `m_working_holes`
- **THEN** `mo->sla_drain_holes` SHALL remain unchanged
- **THEN** no undo snapshot SHALL be created

#### Scenario: Size change (diameter/depth slider) writes to m_working_holes only

- **WHEN** the user changes the diameter or depth slider (including commit)
- **THEN** the updated radius or height SHALL be written to the selected holes in `m_working_holes`
- **THEN** `mo->sla_drain_holes` SHALL remain unchanged
- **THEN** no undo snapshot SHALL be created

#### Scenario: m_working_holes initialized at session start

- **WHEN** the Drill gizmo is activated on an object that has existing `sla_drain_holes`
- **THEN** `m_working_holes` SHALL be set to a copy of `mo->sla_drain_holes`
- **THEN** the user SHALL see the existing holes rendered correctly

---

### Requirement: Apply is the sole undo/redo boundary for Drill

GLGizmoDrill Apply SHALL be the only operation that writes to `mo->sla_drain_holes` and creates an undo snapshot. The Apply sequence SHALL be:
1. `Plater::TakeSnapshot(wxGetApp().plater(), "Apply drain holes")`
2. `mo->sla_drain_holes = m_working_holes`
3. `reslice_until_step(slaposDrillHoles)`

#### Scenario: Apply creates exactly one undo snapshot

- **WHEN** the user adds multiple holes and then clicks Apply
- **THEN** exactly one snapshot named "Apply drain holes" SHALL appear in the undo stack
- **THEN** `mo->sla_drain_holes` SHALL equal `m_working_holes` after Apply

#### Scenario: Undo after Apply restores to pre-Apply state

- **WHEN** the user adds holes H1, H2, H3 and clicks Apply
- **WHEN** the user then presses Ctrl+Z (undo)
- **THEN** `mo->sla_drain_holes` SHALL be restored to its state before Apply was clicked
- **THEN** the rendered holes SHALL reflect the restored state

#### Scenario: Multiple Apply cycles each create independent snapshots

- **WHEN** the user applies session A (holes H1, H2), then applies session B (holes H1, H2, H3)
- **WHEN** the user presses Ctrl+Z once
- **THEN** `mo->sla_drain_holes` SHALL revert to the state after session A Apply (H1, H2)
- **WHEN** the user presses Ctrl+Z again
- **THEN** `mo->sla_drain_holes` SHALL revert to the state before session A Apply

#### Scenario: No snapshot is created for intermediate Add/Delete/Move/Size operations

- **WHEN** the user performs any combination of Add, Delete, Move, Size changes without clicking Apply
- **THEN** the undo stack SHALL contain no new snapshots from those operations
- **THEN** pressing Ctrl+Z SHALL jump to the previous Apply boundary (or pre-entry state if no Apply occurred in this session)

---

### Requirement: data_changed(is_serializing=true) rebuilds m_working_holes from restored model

After an undo or redo operation restores `mo->sla_drain_holes` via cereal deserialization, `data_changed(is_serializing=true)` SHALL rebuild `m_working_holes` from the restored `mo->sla_drain_holes`, then call `reload_cache()` and `unregister_hole_raycasters_for_picking()` so that subsequent rendering reflects the restored state.

#### Scenario: Undo after Apply updates m_working_holes to restored state

- **WHEN** the user applies holes and then presses Ctrl+Z while Drill gizmo is active
- **THEN** `data_changed(is_serializing=true)` SHALL be called
- **THEN** `m_working_holes` SHALL equal the cereal-restored `mo->sla_drain_holes`
- **THEN** the rendered holes SHALL match the restored model state

#### Scenario: Redo after Undo correctly updates m_working_holes

- **WHEN** the user presses Ctrl+Z then Ctrl+Y (undo then redo) while Drill gizmo is active
- **THEN** `m_working_holes` SHALL reflect the redo-restored `mo->sla_drain_holes`
- **THEN** raycasters SHALL be re-registered with the correct hole set

#### Scenario: No crash during Undo/Redo in active Drill session

- **WHEN** the user presses Ctrl+Z multiple times in quick succession while Drill gizmo is active
- **THEN** the application SHALL NOT crash
- **THEN** `m_selected` size SHALL equal `m_working_holes.size()` after each undo

---

### Requirement: Exit Drill discards m_working_holes without restoring sla_drain_holes

When the Drill gizmo is deactivated (state transitions to Off), `m_working_holes` SHALL be cleared and `mo->sla_drain_holes` SHALL NOT be modified. The model retains whatever was last written by Apply (or the state at gizmo entry if Apply was never called).

#### Scenario: Exit without Apply leaves model unchanged

- **WHEN** the user adds holes in `m_working_holes` without clicking Apply, then exits Drill
- **THEN** `mo->sla_drain_holes` SHALL be unchanged from when the Drill session started
- **THEN** the added holes SHALL NOT appear in the model after exit

#### Scenario: Exit after Apply retains Applied state

- **WHEN** the user applies holes (Apply writes `mo->sla_drain_holes`), then exits Drill without further changes
- **THEN** `mo->sla_drain_holes` SHALL retain the state written by Apply
- **THEN** `m_working_holes` SHALL be cleared on exit

#### Scenario: Exit after partial work discards unapplied changes only

- **WHEN** the user applies once (H1, H2 applied), then adds H3 in `m_working_holes` without a second Apply, then exits
- **THEN** `mo->sla_drain_holes` SHALL contain H1 and H2 (last Apply state)
- **THEN** H3 SHALL be discarded
