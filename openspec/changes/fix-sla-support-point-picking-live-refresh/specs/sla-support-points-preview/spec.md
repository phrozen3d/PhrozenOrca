## ADDED Requirements

### Requirement: Picking raycaster transforms stay live during Manual Editing

While `m_editing_mode` is true, `update_point_raycasters_for_picking_transform()` SHALL be invoked every render frame (alongside `render_points()`), not only from discrete trigger events (entering editing mode, raycaster registration, point drag, `data_changed()`). The pickable volume of every support point SHALL track the same live Process-tab Top parameter values used by the rendered cone in that same frame.

#### Scenario: Adjusting Upper Diameter grows the pickable pin sphere immediately

- **GIVEN** Manual Editing mode with an auto-generated support point that has no explicit per-point geometry
- **WHEN** the user increases "Upper Diameter" in the Process tab, without selecting the point or dragging it
- **THEN** the rendered pin sphere grows on the next frame
- **AND** the pickable `pin_sphere` raycaster radius grows to match on that same frame — hovering at the new visible edge reports a hit

#### Scenario: Adjusting Lower Diameter moves the pickable back sphere immediately

- **GIVEN** Manual Editing mode with an auto-generated support point that has no explicit per-point geometry
- **WHEN** the user increases "Lower Diameter" in the Process tab, without selecting the point or dragging it
- **THEN** the rendered back sphere grows and moves further from the anchor on the next frame
- **AND** the pickable `back_sphere` raycaster's position and radius track the new geometry on that same frame — there is no stale hit-test region left behind at the old, smaller position

#### Scenario: No dead zone accumulates from repeated live edits

- **GIVEN** Manual Editing mode with several unselected auto-generated points visible
- **WHEN** the user repeatedly adjusts Top parameters (Upper Diameter, Lower Diameter, Segment Length) back and forth without ever selecting or dragging a point
- **THEN** at every point in time, hovering over the currently visible pinhead reports a hit
- **AND** hovering just outside the currently visible pinhead does not report a hit (no leftover pickable region from an earlier parameter value)

#### Scenario: Dragging a point still works after a live parameter edit

- **GIVEN** Manual Editing mode with a support point whose Top parameters were just live-edited while unselected
- **WHEN** the user hovers, selects, and drags that point
- **THEN** hover, selection, and drag behave exactly as before this change — the added per-frame refresh does not interfere with the drag's own transform updates
