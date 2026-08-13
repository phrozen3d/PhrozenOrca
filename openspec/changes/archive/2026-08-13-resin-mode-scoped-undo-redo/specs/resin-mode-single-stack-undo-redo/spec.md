## ADDED Requirements

### Requirement: Resin mode operations are recorded on the single main undo/redo stack

Every undoable operation performed inside any of the three resin edit modes (Generate support, Hollow, Drill) — including but not limited to Apply-class commits, individual support point add/move/delete, and Hollow/Drill Apply — SHALL be recorded as its own independent snapshot directly on the main plater undo/redo stack. No mode SHALL open a secondary/scoped undo stack, and no set of operations performed within a mode session SHALL be collapsed or merged into a single snapshot.

#### Scenario: Each Apply is independently undoable

- **WHEN** the user performs multiple Apply-class commits within a single mode session (e.g. two Hollow Applies, or an Auto-generate followed by adding points)
- **THEN** each commit SHALL be undoable and redoable independently, in the order it was performed

#### Scenario: In-mode edits before and after an Apply remain individually undoable

- **WHEN** the user, while staying inside SlaSupports Manual editing, adds a point, presses Apply, then adds another point
- **THEN** a single Ctrl+Z SHALL undo only the most recent point addition
- **THEN** the point added before Apply and the Apply commit itself SHALL remain intact

---

### Requirement: Undo/redo may open or close a mode's panel as a side effect, and this is not an error

Because gizmo activation state is part of what a snapshot captures (alongside Model/Selection/PartPlateList), restoring an older or newer snapshot may open or close a resin mode's panel if that snapshot was taken while the panel was open or closed. This SHALL be treated as ordinary, expected undo/redo behavior — the system SHALL NOT attempt to prevent undo/redo from crossing a mode's open/close boundary, and SHALL NOT get stuck or refuse to continue undoing/redoing past such a boundary.

#### Scenario: Undo past a mode-opening snapshot reopens the mode

- **WHEN** the user undoes to a point in history that was captured while a resin mode's panel was open
- **THEN** the panel SHALL open as part of restoring that state
- **THEN** the application SHALL NOT crash

#### Scenario: Continued undo/redo across a mode boundary does not get stuck

- **WHEN** the user continues to press undo (or redo) after crossing a point where a mode's panel opened or closed as a side effect
- **THEN** undo/redo SHALL continue to operate on the ordinary main-stack history before/after that point
- **THEN** the application SHALL NOT refuse further undo/redo or otherwise become unresponsive because of the mode boundary

---

### Requirement: SLA backend support geometry re-syncs after undo/redo

When an undo/redo operation restores a state while the Generate support gizmo is the active gizmo (and it is not in Manual editing mode), the system SHALL reload the display cache from the restored model and re-invalidate/re-run the appropriate SLA backend step(s) (support points, or pad/support-tree if Structure view is selected) so that the rendered support geometry matches the restored model state. This SHALL NOT depend on whether backend supports existed at the moment the snapshot being restored to was originally taken.

#### Scenario: Undoing an Auto Apply clears stale pad/tree mesh

- **WHEN** the user applies Auto support generation, switches to Structure view, leaves the mode, and then undoes the Apply
- **THEN** the previously rendered pad/support-tree mesh SHALL be cleared or regenerated to match the restored (un-applied) state
- **THEN** it SHALL NOT remain visibly stale

---

### Requirement: Leaving the Generate support panel always leaves the actual support structure visible

When the Generate support gizmo panel is closed (regardless of whether Auto or Manual mode, or the Points/Structure sub-view, was active immediately before closing), the system SHALL ensure any successfully generated support structure is visible in the normal 3D view.

#### Scenario: Closing the panel while Points sub-view was selected still shows generated supports

- **WHEN** the user applies Auto support generation, switches to the Points sub-view, and closes the panel
- **THEN** the generated support structure SHALL be visible in the normal 3D view after closing

> Note: undo/redo landing on a point in history where the focused ModelObject no longer exists is covered by the `resin-mode-structural-mutation-safety` capability's "A gizmo self-closes safely when its focused ModelObject disappears" requirement, not restated here.
