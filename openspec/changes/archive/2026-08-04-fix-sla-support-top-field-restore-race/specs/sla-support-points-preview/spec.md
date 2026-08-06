## MODIFIED Requirements

### Requirement: Live Top parameter reads are isolated from the per-point display borrow

While the Process → Support → Top fields are displaying a selected support point's own stored values — whether because a point is currently selected (`GLGizmoSlaSupports::has_selected_support_points() == true`) or because the display has not yet been restored to the live preset after the point was deselected (`TabSLAPrint::is_support_point_top_field_active() == true`) — the live-parameter read helpers used to resolve OTHER points' preview geometry (`process_top_float_live()`, `process_contact_type_is_sphere()`) SHALL NOT read the widgets' currently-displayed text. They SHALL instead read the actual live SLA print preset value (`sla_process_config()`).

The two conditions SHALL be combined with logical OR, not one replacing the other: `has_selected_support_points()` covers the window right after selecting a point, before `begin_support_point_top_field_display()` has updated the widgets to that point's values; `is_support_point_top_field_active()` covers the window right after deselecting, before the deferred `end_support_point_top_field_display()` (scheduled via `wxTheApp->CallAfter()` in `notify_process_tab_selection_changed()`) has restored the widgets to the live preset text.

When neither condition holds — no point selected and the widgets are not mid-restore — these helpers SHALL continue to read the widgets' currently-displayed text, preserving the existing live-typing behavior (values reflect edits before the field loses focus).

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

#### Scenario: Deselecting does not cause a one-frame flash of the just-edited point's values

- **GIVEN** the user has selected an auto-generated point, edited its `support_head_front_diameter` or `support_contact_type`, and other auto-generated points are visible with different values
- **WHEN** the user deselects the point (e.g. by clicking elsewhere in the viewport, which clears the selection synchronously while the Top field restore is deferred to the next event-loop idle tick)
- **THEN** the other auto-generated points' preview does NOT flash to the just-deselected point's values on any frame, including the frame(s) rendered before the deferred restore has run
- **AND** once the restore has run, the other points continue to reflect the actual live preset value with no visible transition

#### Scenario: Switching the selected point does not leak the previous point's displayed value

- **GIVEN** two support points, A and B, both without explicit geometry
- **WHEN** the user selects A, then immediately selects B (switching selection without an intervening deselected state)
- **THEN** at no point do the other, unselected auto-generated points' preview reflect A's or B's per-point displayed values — they continue to track the live preset throughout the switch
