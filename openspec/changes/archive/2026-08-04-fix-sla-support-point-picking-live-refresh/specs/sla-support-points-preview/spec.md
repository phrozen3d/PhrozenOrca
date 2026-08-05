## ADDED Requirements

### Requirement: Picking raycaster transforms stay live during Manual Editing

While `m_editing_mode` is true, `update_point_raycasters_for_picking_transform()` SHALL be invoked every render frame (alongside `render_points()`), not only from discrete trigger events (entering editing mode, raycaster registration, point drag, `data_changed()`). The pickable volume of every support point SHALL track the same live Process-tab Top parameter values used by the rendered cone in that same frame. `update_point_raycasters_for_picking_transform()` SHALL derive each raycaster's active state from `is_mesh_point_clipped()` itself rather than relying on `render_points()` having run in a particular order within the same frame.

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

#### Scenario: A clipped point never intercepts hover meant for another point

- **GIVEN** the object clipper hides an upper support point, exposing a lower point whose on-screen footprint overlaps where the hidden point used to be
- **WHEN** the user hovers over that overlapping region, in Manual Editing mode with `update_point_raycasters_for_picking_transform()` running every frame
- **THEN** the now-visible lower point is hovered
- **AND** the hidden upper point's `pin_sphere`/`cone`/`back_sphere` do not intercept the hit, in any frame, regardless of the order `render_points()` and `update_point_raycasters_for_picking_transform()` ran in that frame

## MODIFIED Requirements

### Requirement: Points-preview picking is consistent with the rendered cone

The pickable volume of a Points-preview support point SHALL be the union of its three registered raycasters — `pin_sphere` covering the pin / contact sphere, `cone` covering the robe segment, and `back_sphere` covering the pillar-junction end — and SHALL cover the entire visible pinhead.

`pin_sphere` SHALL be centred at the pin/contact sphere's actual rendered centre — `instance_scaling_matrix * sp.pos + (head.r_pin_mm - head.penetration_mm) * head.dir`, not at the anchor itself — with radius `max(head.r_pin_mm, head.r_contact_mm)` in mm regardless of instance scale. (`head_mesh_body()` places the pin/contact sphere at local `z = penetration_mm - r_pin_mm`, not at the anchor's local origin; centring the raycaster at the anchor directly is a mismatch that grows more visible as Contact Diameter increases the sphere's radius.)

`cone` SHALL be transformed so the unit cone (`its_make_cone(1.0, 1.0, …)`, base circle at the origin in the `z = 0` plane, apex at `+Z·h`) aligns with the rendered robe: apex at the anchor, base of radius `head.r_back_mm` at offset `head.fullwidth() - head.r_back_mm` along the surface normal used to orient the rendered cone. It SHALL NOT remain at `Transform3d::Identity()`, and it SHALL NOT be permanently inactive.

`back_sphere` SHALL be centred at the same point as `cone`'s base (offset `head.fullwidth() - head.r_back_mm` along the surface normal) with radius `head.r_back_mm` in mm, matching the visible back sphere exactly — `cone`'s flat base cannot follow the back sphere's curvature past its widest point, so without `back_sphere` the outer half of the back ball is unhoverable.

All three raycasters SHALL use the same picking transform convention as the render path — positive instance scale excluded, mirror preserved — and SHALL be registered under the same raycaster id so a hit on any of them reports the same `m_hover_id`.

All three raycasters SHALL follow the same clipping-driven active-state rule: when a point is clipped, none of them is active. This rule SHALL hold regardless of how many times per frame, or in what order relative to `render_points()`, the raycaster transforms are recomputed.

The pickable volume SHALL NOT extend beyond the visible pinhead: `cone` is inscribed within the rendered frustum (which widens from `r_pin` to `r_back`), and `back_sphere` coincides with the rendered back sphere, so empty space adjacent to the rendered geometry SHALL NOT register a hit.

#### Scenario: Hover on the exposed cone body

- **GIVEN** support points have been generated and are visible in Points view
- **WHEN** the user hovers over the robe segment of a pinhead, away from its top sphere
- **THEN** the gizmo reports a hover-id matching that point
- **AND** clicking there selects that point

#### Scenario: Hover on the back sphere

- **GIVEN** support points have been generated and are visible in Points view
- **WHEN** the user hovers over the back sphere — the end of the pinhead beyond `cone`'s base, where the visible ball curves back inward toward its own tip
- **THEN** the gizmo reports a hover-id matching that point

#### Scenario: Hover on an enlarged contact sphere

- **GIVEN** Manual Editing mode with a support point whose Contact Diameter is set well above the pin diameter, so `pin_sphere`'s radius is driven by `head.r_contact_mm`
- **WHEN** the user hovers anywhere on the visible contact sphere's surface, including the side furthest from the back sphere
- **THEN** the gizmo reports a hover-id matching that point
- **AND** hovering just outside that same surface does not report a hit

#### Scenario: Hover under uniform scale

- **GIVEN** an instance with uniform scale 1.5 in Manual Editing mode
- **WHEN** the user hovers over the visible cone for a support point
- **THEN** the gizmo reports a hover-id matching that point
- **AND** every part of the visible pinhead — top sphere, robe, back sphere — is hoverable

#### Scenario: Hover under non-uniform scale

- **GIVEN** an instance with non-uniform scale in Manual Editing mode
- **WHEN** the user hovers over the visible cone for a support point
- **THEN** the `pin_sphere` centre coincides with the visible pin/contact sphere's actual centre (not the anchor)
- **AND** the `cone` and `back_sphere` axis/position follow the same scaled-mesh surface normal that orients the rendered cone
- **AND** there is no hover gap along the length of the visible pinhead

#### Scenario: No false hits beside the cone

- **GIVEN** a support point rendered in Points view
- **WHEN** the user hovers just outside the silhouette of the visible pinhead
- **THEN** no hover-id is reported for that point

#### Scenario: Mirrored instance

- **GIVEN** an instance with a mirror transform so `vol->is_left_handed()` is true
- **WHEN** the user hovers over the robe segment of a pinhead
- **THEN** the point is hovered
- **AND** the cone raycaster is not flipped away from the visible geometry

#### Scenario: Clipped point disables all raycasters

- **GIVEN** the object clipper is active and a support point is clipped
- **WHEN** the user hovers at that point's former location
- **THEN** none of `pin_sphere`, `cone`, or `back_sphere` reports a hit
