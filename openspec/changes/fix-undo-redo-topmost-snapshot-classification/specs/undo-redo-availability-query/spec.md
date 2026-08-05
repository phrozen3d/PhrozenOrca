## ADDED Requirements

### Requirement: Undo availability reflects a real, reachable earlier state
`UndoRedo::StackImpl::has_undo_snapshot()` SHALL return `true` if and only if there exists a snapshot strictly before the active position that `undo()`'s own boundary resolution (the `SIZE_MAX` branch) would actually jump to — i.e. the active position is not already adjacent to `m_snapshots.begin()`. It SHALL NOT count the origin/first stack entry as a valid undo target once the active position has already been resolved down to it, and it SHALL NOT be fooled by the topmost placeholder entry (`Snapshot::is_topmost()`) inheriting a project-modifying `snapshot_type` from the action that preceded it.

#### Scenario: Fresh project reports no undo available
- **WHEN** exactly one named snapshot (e.g. `"New Project"`) has ever been taken, and no further action has occurred
- **THEN** `has_undo_snapshot()` returns `false`, matching what `undo()`'s `SIZE_MAX` resolution would do if invoked (return `false`, no-op)

#### Scenario: One real action makes undo available
- **WHEN** at least one project-modifying action (e.g. `"Add Primitive"`) has been taken after the initial project snapshot
- **THEN** `has_undo_snapshot()` returns `true`

#### Scenario: Undoing back to the origin makes undo unavailable again
- **WHEN** the active position has been walked back (via `undo()`) until it sits immediately after the very first stack entry
- **THEN** `has_undo_snapshot()` returns `false`

### Requirement: Redo availability reflects a real, reachable forward state
`UndoRedo::StackImpl::has_redo_snapshot()` SHALL return `true` if and only if there exists a snapshot strictly after the active position that `redo()`'s own boundary resolution (the `SIZE_MAX` branch) would actually jump to. It SHALL NOT include the entry at the active position itself in its classification, even when that entry (e.g. the topmost placeholder) carries a project-modifying `snapshot_type` inherited from a prior action.

#### Scenario: Sitting at the topmost state reports no redo available
- **WHEN** the active position is the topmost/current state (no undo has occurred since the last action, or none has ever occurred)
- **THEN** `has_redo_snapshot()` returns `false`

#### Scenario: After undoing, redo becomes available
- **WHEN** the active position has been moved backward via `undo()` at least once, leaving at least one entry strictly after it
- **THEN** `has_redo_snapshot()` returns `true`

#### Scenario: Redoing back to the topmost state makes redo unavailable again
- **WHEN** the active position has been advanced via `redo()` until it reaches the last entry in the stack
- **THEN** `has_redo_snapshot()` returns `false`

### Requirement: Availability queries agree with the actual jump outcome
For any stack state, `has_undo_snapshot()` SHALL return `true` if and only if calling `undo(..., time_to_load = SIZE_MAX)` would return `true` (and analogously for `has_redo_snapshot()` and `redo(..., time_to_load = SIZE_MAX)`), without actually performing the jump. This keeps the read-only availability query and the state-mutating jump from disagreeing, which is the specific defect this change corrects.

#### Scenario: No stack state where availability and jump outcome disagree
- **WHEN** `has_undo_snapshot()` is checked at any point during a session (fresh project, after N actions, after M undos, after redoing)
- **THEN** its result matches whether `undo(SIZE_MAX)` would succeed at that same point (and analogously, `has_redo_snapshot()` matches `redo(SIZE_MAX)`)
