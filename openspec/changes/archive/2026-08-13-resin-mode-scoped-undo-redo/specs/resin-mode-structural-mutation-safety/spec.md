## ADDED Requirements

> Scope note (2026-08-09, Decision G): this capability originally also included a "structural mutations force-collapse an active mode sub-stack" requirement and an "enter/leave idempotency" requirement — both existed to protect the scoped sub-stack (`resin-mode-scoped-undo-stack`, abandoned) from being stranded by a structural mutation. That capability no longer exists, so those two requirements were removed rather than carried forward describing a mechanism that isn't there. The one requirement below is orthogonal to stack architecture and remains fully in effect.

### Requirement: A gizmo self-closes safely when its focused ModelObject disappears

Each of the three resin mode gizmos (Generate support / Hollow / Drill) SHALL detect — in its data-refresh / render / leave paths — that its focused ModelObject no longer exists (selection empty or model object null), and SHALL safely close itself without dereferencing the freed object.

#### Scenario: Object freed while gizmo open does not crash on next refresh

- **WHEN** the focused ModelObject of an open resin mode gizmo is freed (e.g. deleted while the gizmo is open)
- **THEN** the gizmo's next data-refresh / render SHALL detect the missing object and SHALL NOT dereference the freed ModelObject
- **THEN** the application SHALL NOT crash

#### Scenario: Leaving a mode after its object vanished does not crash

- **WHEN** the focused ModelObject has been freed and the mode is subsequently left (or self-closes)
- **THEN** the leave path SHALL NOT reference the freed object
- **THEN** the application SHALL NOT crash
