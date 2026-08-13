# resin-mode-structural-mutation-safety Specification

## Purpose
TBD - created by archiving change resin-mode-scoped-undo-redo. Update Purpose after archive.
## Requirements
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

