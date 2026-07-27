## ADDED Requirements

### Requirement: Structural mutations force-collapse an active mode sub-stack before their snapshot lands

When a structural mutation (any operation that deletes, replaces, or rebuilds the model — including but not limited to deleting an object, deleting an instance, clearing a plate, and New / Open / import / reload / replace-mesh of a project) begins while a resin edit mode sub-stack is active, the system SHALL force the active mode session to collapse and switch the active undo stack back to the main plater stack BEFORE the mutation's own snapshot is taken. As a result, the mutation's snapshot SHALL land on the main plater stack, never on the mode sub-stack.

This SHALL be implemented as a containment invariant applied at the mutation entry path, so that it covers all structural mutation entry points without requiring each to be individually enumerated or disabled.

#### Scenario: Deleting the focused object while in a mode lands the delete on the main stack

- **WHEN** a resin edit mode is active and the user deletes the model object being edited
- **THEN** the mode session SHALL be collapsed first (recording its result on the main stack if there was a net change, or nothing if it was a no-op)
- **THEN** the delete snapshot SHALL be recorded on the main plater stack, not on the mode sub-stack

#### Scenario: Delete via the Objects-panel path is also covered

- **WHEN** a resin edit mode is active and the user triggers deletion through the Objects-panel Delete path (the path that bypasses the main delete gate)
- **THEN** the active mode sub-stack SHALL still be collapsed before the delete snapshot lands
- **THEN** the delete snapshot SHALL be recorded on the main plater stack

#### Scenario: Clearing the plate or loading a project while in a mode is safe

- **WHEN** a resin edit mode is active and the user clears the current plate or loads / opens / imports a project
- **THEN** the active mode sub-stack SHALL be collapsed before the structural mutation's snapshot lands on the main stack
- **THEN** the resulting undo history SHALL NOT contain a snapshot stranded on the mode sub-stack

---

### Requirement: A gizmo self-closes safely when its focused ModelObject disappears

As a final fallback that does not depend on enumerating mutation entry points, each of the three resin mode gizmos SHALL detect — in its data-refresh / render / leave paths — that its focused ModelObject no longer exists (selection empty or model object null), and SHALL safely close itself without dereferencing the freed object. This guarantee SHALL hold even if a structural mutation reaches the model without going through the containment invariant above.

#### Scenario: Object freed while gizmo open does not crash on next refresh

- **WHEN** the focused ModelObject of an open resin mode gizmo is freed
- **THEN** the gizmo's next data-refresh / render SHALL detect the missing object and SHALL NOT dereference the freed ModelObject
- **THEN** the application SHALL NOT crash

#### Scenario: Leaving a mode after its object vanished records nothing and does not crash

- **WHEN** the focused ModelObject has been freed and the mode is subsequently left (or force-collapsed)
- **THEN** the leave path SHALL treat the session as a no-op and SHALL NOT record a main-stack snapshot referencing the freed object
- **THEN** the application SHALL NOT crash

---

### Requirement: Enter/leave of the mode sub-stack is idempotent

The shared enter/leave mechanism SHALL be robust against being called in an unexpected active-stack state. Leaving when the active stack is already the main stack SHALL be a safe no-op. Entering when a sub-stack is already active SHALL first collapse the existing session rather than asserting or corrupting stack state.

#### Scenario: Redundant leave is a safe no-op

- **WHEN** the leave path runs while the active stack is already the main plater stack
- **THEN** the operation SHALL be a no-op and SHALL NOT crash or corrupt the undo stacks

#### Scenario: Entering while a sub-stack is already active collapses first

- **WHEN** the enter path runs while a mode sub-stack is still active (e.g., switching directly from one resin mode to another)
- **THEN** the existing session SHALL be collapsed first, and a fresh baseline SHALL be anchored for the new mode
- **THEN** edits from the two modes SHALL NOT be mixed into a single sub-stack
