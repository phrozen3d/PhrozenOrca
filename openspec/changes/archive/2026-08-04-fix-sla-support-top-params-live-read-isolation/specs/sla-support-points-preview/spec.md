## ADDED Requirements

### Requirement: Live Top parameter reads are isolated from the per-point display borrow

When a support point is selected (`GLGizmoSlaSupports::has_selected_support_points() == true`) and the Process → Support → Top fields are consequently displaying that point's own stored values, the live-parameter read helpers used to resolve OTHER points' preview geometry (`process_top_float_live()`, `process_contact_type_is_sphere()`) SHALL NOT read the widgets' currently-displayed text. They SHALL instead read the actual live SLA print preset value (`sla_process_config()`).

When no support point is selected, these helpers SHALL continue to read the widgets' currently-displayed text, preserving the existing live-typing behavior (values reflect edits before the field loses focus).

#### Scenario: Selecting and editing a manual point does not perturb auto points

- **GIVEN** Points view with several auto-generated points and one `manual_add` point with explicit geometry
- **WHEN** the user selects the `manual_add` point and edits its `support_head_front_diameter` in the Top fields
- **THEN** the auto-generated points' preview cone diameters remain unchanged for the entire duration the point stays selected
- **AND** they do NOT visually shift to match the value currently displayed for the selected point

#### Scenario: Selecting and editing an auto-generated point does not perturb other auto points

- **GIVEN** Points view with multiple auto-generated points, none with explicit geometry
- **WHEN** the user selects one auto-generated point and edits its `support_head_front_diameter`
- **THEN** the OTHER auto-generated points' preview cone diameters remain unchanged
- **AND** only the selected point's own preview reflects the edit (via its own stored geometry, independent of this requirement)

#### Scenario: Contact type selection while a point is selected does not perturb other points

- **GIVEN** Points view with auto-generated points and a selected manual point
- **WHEN** the user changes the selected point's `support_contact_type` between None and Sphere
- **THEN** the other auto-generated points' contact-sphere rendering is unaffected for the duration of the selection

#### Scenario: Live-typing feedback is preserved when nothing is selected

- **GIVEN** Points view with auto-generated points, no support point currently selected
- **WHEN** the user edits `support_head_front_diameter` in the Process tab and the view redraws before the field loses focus
- **THEN** the auto-generated points' preview cone diameters follow the newly typed value on the next redraw
- **AND** this live-typing behavior is unchanged from before this change

#### Scenario: Deselecting restores the true preset-driven appearance

- **GIVEN** the scenario in "Selecting and editing a manual point does not perturb auto points" has just occurred
- **WHEN** the user deselects the point
- **THEN** the auto-generated points continue to reflect the actual live preset value, with no transition glitch, since they were never perturbed in the first place
