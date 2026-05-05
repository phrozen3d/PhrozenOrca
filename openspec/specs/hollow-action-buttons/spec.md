## ADDED Requirements

### Requirement: Hollow operation uses action buttons instead of checkbox-preview flow
The system SHALL provide Hollow and Remove action buttons to replace the previous checkbox + Preview two-step flow. Each button SHALL atomically set the hollow state on the current object and trigger a preview update.

#### Scenario: User presses Hollow button
- **WHEN** the user presses the Hollow button in the Hollow gizmo panel
- **THEN** the system SHALL set `hollowing_enable = true` on the current object's config
- **THEN** the system SHALL trigger `reslice_until_step(slaposDrillHoles)` to update the hollow preview
- **THEN** the Remove button SHALL become enabled in the same panel

#### Scenario: User presses Remove button
- **WHEN** the user presses the Remove button in the Hollow gizmo panel
- **THEN** the system SHALL set `hollowing_enable = false` on the current object's config
- **THEN** the system SHALL trigger `reslice_until_step(slaposDrillHoles)` to update the preview to the original mesh
- **THEN** the Remove button SHALL become disabled in the same panel

#### Scenario: Remove button is disabled when object is not hollowed
- **WHEN** the current object has `hollowing_enable = false`
- **THEN** the Remove button SHALL be disabled (greyed out) and non-interactive

#### Scenario: Hollow and Remove buttons are disabled when input is not available
- **WHEN** `is_input_enabled()` returns false (e.g., object not on active bed)
- **THEN** both the Hollow button and the Remove button SHALL be disabled

### Requirement: Hollow parameters use a pending-apply model
The hollow parameter controls (wall thickness slider/input, closing distance slider/input, quality slider/input) SHALL remain interactive regardless of hollow state, but SHALL NOT write to the object's config on every slider interaction. Changes SHALL be held in UI-local pending state and written to config only when the Hollow button is pressed.

#### Scenario: Edit parameters before hollowing — no immediate preview change
- **GIVEN** the object has not been hollowed (`hollowing_enable = false`)
- **WHEN** the user drags the wall thickness slider
- **THEN** the slider SHALL move visually and update the pending value in memory
- **THEN** `mo->config["hollowing_min_thickness"]` SHALL NOT be modified
- **THEN** the hollow preview SHALL NOT change (no reslice is triggered)

#### Scenario: Edit parameters then press Hollow — pending values applied
- **GIVEN** the user has adjusted hollow parameters (in pending state)
- **WHEN** the user presses the Hollow button
- **THEN** the system SHALL write all pending parameter values to `mo->config`
- **THEN** the system SHALL set `hollowing_enable = true`
- **THEN** the system SHALL trigger `reslice_until_step(slaposDrillHoles)`
- **THEN** the hollow preview SHALL reflect the pending parameter values

#### Scenario: Edit parameters while object is hollowed — no immediate reslice
- **GIVEN** the object is hollowed (`hollowing_enable = true`)
- **WHEN** the user adjusts any hollow parameter slider or input field
- **THEN** the pending value SHALL update in memory
- **THEN** `mo->config` SHALL NOT be modified
- **THEN** no reslice SHALL be triggered (preview remains unchanged)
- **WHEN** the user presses the Hollow button
- **THEN** pending values SHALL be written to config and reslice SHALL be triggered

#### Scenario: Pending values are discarded when leaving the gizmo
- **WHEN** the user adjusts parameters but does not press Hollow
- **AND** the user closes the Hollow gizmo, switches tools, or switches to another object
- **THEN** the unapplied pending values SHALL be discarded
- **WHEN** the user re-enters the Hollow gizmo for the same object
- **THEN** the parameter sliders SHALL show the values from `mo->config` (the last committed values), not the discarded pending adjustments

#### Scenario: Remove Hollow does not apply pending parameters
- **GIVEN** the user has adjusted parameters (in pending state)
- **WHEN** the user presses the Remove button
- **THEN** the system SHALL set `hollowing_enable = false` and reslice
- **THEN** the pending parameter values SHALL NOT be written to `mo->config`
- **THEN** `mo->config["hollowing_min_thickness"]` (and quality, closing distance) SHALL retain the values from the last Hollow button press

### Requirement: Hollow state and parameters remain per-object
Each object's hollow state (`hollowing_enable`) and hollow parameters SHALL be stored independently per object. Hollow or Remove operations on one object SHALL NOT affect any other object's hollow state or parameters.

#### Scenario: Switching between objects preserves per-object hollow state
- **GIVEN** object A is hollowed (`hollowing_enable = true`) and object B is not
- **WHEN** the user selects object B in the Hollow gizmo
- **THEN** the Remove button SHALL be disabled (object B is not hollowed)
- **THEN** the parameter sliders SHALL reflect object B's own parameter values
- **WHEN** the user switches back to object A
- **THEN** the Remove button SHALL be enabled (object A is still hollowed)
- **THEN** the parameter sliders SHALL reflect object A's own parameter values

### Requirement: Slicing behavior is unchanged
The `hollowing_enable` value written by the Hollow / Remove buttons SHALL be consumed by the SLA slicing pipeline identically to the value previously set by the checkbox.

#### Scenario: Hollow button causes slicing to produce hollowed output
- **WHEN** the user presses the Hollow button and then slices the object
- **THEN** the SLA slice output SHALL include the hollowed mesh geometry

#### Scenario: Remove button causes slicing to produce non-hollowed output
- **WHEN** the user presses the Remove button and then slices the object
- **THEN** the SLA slice output SHALL use the original (non-hollowed) mesh geometry

### Requirement: Drill holes are not affected by Hollow / Remove operations
Pressing Hollow or Remove SHALL NOT clear or modify the object's drill holes data (`mo->sla_drain_holes`).

#### Scenario: Drill holes persist after Remove
- **GIVEN** the object has both hollow enabled and drill holes defined
- **WHEN** the user presses Remove
- **THEN** the drill holes SHALL remain in `mo->sla_drain_holes`
- **THEN** the preview SHALL show the original mesh with drill holes applied (hollow removed, drill retained)
