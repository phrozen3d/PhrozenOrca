## ADDED Requirements

### Requirement: Preview cone geometry parameters match the slicing pipeline

The per-point geometry parameters used to build a Points-preview cone SHALL be resolved with the same rule the slicing pipeline uses, and SHALL NOT depend on whether the gizmo is in editing mode.

For every support point, each of head front radius, head back radius, head width, head penetration, and contact sphere radius SHALL be taken from the point's own stored value when that value is set (`>= 0`, i.e. not `SUPPORT_POINT_USE_PRESET`), and SHALL fall back to the live SLA print preset value otherwise — the rule implemented by the shared `point_*()` helpers in `SupportPoint.hpp` and consumed unconditionally by `SupportTreeBuildsteps`.

Points whose `type` is not `manual_add`, and `manual_add` points with no explicit geometry, SHALL continue to resolve entirely from the preset.

#### Scenario: Manual points keep their size after leaving editing mode

- **GIVEN** the user has placed three `manual_add` support points in Manual editing mode with different `support_head_front_diameter` values
- **WHEN** the user leaves editing mode and views the same points in Points view
- **THEN** each cone keeps the head diameter it was created with
- **AND** the three cones remain visibly different in size

#### Scenario: Preview matches sliced support geometry

- **GIVEN** a model with `manual_add` support points carrying explicit per-point geometry
- **AND** the SLA print preset's Top values differ from those per-point values
- **WHEN** the object is sliced and the resulting support tree is compared against the non-editing Points preview
- **THEN** each preview cone's head diameter, head width, penetration and contact sphere radius match the generated support geometry for the same point

#### Scenario: Auto-generated points are unaffected

- **GIVEN** support points produced by auto-generation, whose per-point geometry fields are all `SUPPORT_POINT_USE_PRESET`
- **WHEN** the gizmo renders them in Points view
- **THEN** every cone resolves its geometry from the live preset values
- **AND** the rendered size is identical to the behaviour before this change

#### Scenario: Editing mode behaviour is unchanged

- **GIVEN** Manual editing mode with a mix of auto and `manual_add` points
- **WHEN** the gizmo renders the points
- **THEN** each cone's geometry is identical to the behaviour before this change
- **AND** a selected point still resolves from its own stored geometry

#### Scenario: Live preset edits still drive points without explicit geometry

- **GIVEN** Points view with auto-generated points and one `manual_add` point that has explicit geometry
- **WHEN** the user changes `support_head_front_diameter` in the SLA print preset and the view is redrawn
- **THEN** the auto points' cone diameter follows the new preset value
- **AND** the `manual_add` point's cone diameter stays at its stored value

### Requirement: Picking stays consistent with the resolved preview geometry

The picking sphere radius for a support point SHALL be derived from the same resolved geometry as the rendered cone, so that changing the geometry resolution rule does not open a gap between what is visible and what can be hit.

#### Scenario: Hover on a manual point with explicit geometry

- **GIVEN** Manual editing mode with a `manual_add` point whose stored head front radius differs from the preset value
- **WHEN** the user hovers over the visible cone for that point
- **THEN** the gizmo reports a hover-id matching that point
- **AND** the picking sphere radius matches the rendered cone's head / contact radius in mm
