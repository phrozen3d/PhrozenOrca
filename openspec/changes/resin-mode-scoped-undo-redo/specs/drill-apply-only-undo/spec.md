## MODIFIED Requirements

### Requirement: Apply is the sole undo/redo boundary for Drill

GLGizmoDrill Apply SHALL be the only operation that writes to `mo->sla_drain_holes` and creates an undo snapshot. The Apply sequence SHALL be:
1. `Plater::TakeSnapshot(wxGetApp().plater(), "Apply drain holes")`
2. `mo->sla_drain_holes = m_working_holes`
3. `reslice_until_step(slaposDrillHoles)`

While Drill is active, this Apply snapshot SHALL be recorded on the **scoped mode sub-stack** (see capability `resin-mode-scoped-undo-stack`), not directly on the main plater stack. Multiple Applies within one Drill session create independent in-session sub-stack steps between which in-session undo/redo can navigate, bounded by the mode-entry baseline. When Drill is left, the whole session SHALL collapse into at most one snapshot on the main plater stack representing the final applied state (or no snapshot if no net change was committed).

#### Scenario: Apply creates exactly one in-session undo snapshot

- **WHEN** the user adds multiple holes and then clicks Apply
- **THEN** exactly one snapshot named "Apply drain holes" SHALL appear on the active Drill sub-stack
- **THEN** `mo->sla_drain_holes` SHALL equal `m_working_holes` after Apply

#### Scenario: In-session undo after Apply restores to pre-Apply state

- **WHEN** the user adds holes H1, H2, H3 and clicks Apply, then presses Ctrl+Z (undo) while Drill is still active
- **THEN** `mo->sla_drain_holes` SHALL be restored to its state before Apply was clicked
- **THEN** the rendered holes SHALL reflect the restored state
- **THEN** undo SHALL NOT restore any state older than the Drill mode-entry baseline

#### Scenario: Multiple Apply cycles create independent in-session snapshots

- **WHEN** the user applies session A (holes H1, H2), then applies session B (holes H1, H2, H3), while remaining in Drill
- **WHEN** the user presses Ctrl+Z once
- **THEN** `mo->sla_drain_holes` SHALL revert to the state after session A Apply (H1, H2)
- **WHEN** the user presses Ctrl+Z again
- **THEN** `mo->sla_drain_holes` SHALL revert to the mode-entry baseline (state before session A Apply) and stop there

#### Scenario: Leaving Drill collapses all Applies into one main snapshot

- **WHEN** the user performs one or more Applies inside Drill and then leaves Drill
- **THEN** the main plater stack SHALL gain exactly one snapshot representing the final applied drain-hole result
- **THEN** a single Ctrl+Z after leaving SHALL restore the state that existed before Drill was opened

#### Scenario: No snapshot is created for intermediate Add/Delete/Move/Size operations

- **WHEN** the user performs any combination of Add, Delete, Move, Size changes without clicking Apply
- **THEN** the sub-stack SHALL contain no new snapshots from those operations
- **THEN** pressing Ctrl+Z SHALL jump to the previous Apply boundary (or the mode-entry baseline if no Apply occurred in this session)
