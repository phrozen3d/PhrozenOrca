## MODIFIED Requirements

### Requirement: Points-preview picking is consistent with the rendered cone

The pickable volume of a Points-preview support point SHALL be the union of its three registered raycasters — `pin_sphere` covering the pin / contact sphere, `cone` covering the robe segment, and `back_sphere` covering the pillar-junction end — and SHALL cover the entire visible pinhead.

`pin_sphere` SHALL be centred at `instance_scaling_matrix * sp.pos` with radius `max(head.r_pin_mm, head.r_contact_mm)` in mm regardless of instance scale.

`cone` SHALL be transformed so the unit cone (`its_make_cone(1.0, 1.0, …)`, base circle at the origin in the `z = 0` plane, apex at `+Z·h`) aligns with the rendered robe: apex at the anchor, base of radius `head.r_back_mm` at offset `head.fullwidth() - head.r_back_mm` along the surface normal used to orient the rendered cone. It SHALL NOT remain at `Transform3d::Identity()`, and it SHALL NOT be permanently inactive.

`back_sphere` SHALL be centred at the same point as `cone`'s base (offset `head.fullwidth() - head.r_back_mm` along the surface normal) with radius `head.r_back_mm` in mm, matching the visible back sphere exactly — `cone`'s flat base cannot follow the back sphere's curvature past its widest point, so without `back_sphere` the outer half of the back ball is unhoverable.

All three raycasters SHALL use the same picking transform convention as the render path — positive instance scale excluded, mirror preserved — and SHALL be registered under the same raycaster id so a hit on any of them reports the same `m_hover_id`.

All three raycasters SHALL follow the same clipping-driven active-state rule: when a point is clipped, none of them is active.

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

#### Scenario: Hover under uniform scale

- **GIVEN** an instance with uniform scale 1.5 in Manual Editing mode
- **WHEN** the user hovers over the visible cone for a support point
- **THEN** the gizmo reports a hover-id matching that point
- **AND** every part of the visible pinhead — top sphere, robe, back sphere — is hoverable

#### Scenario: Hover under non-uniform scale

- **GIVEN** an instance with non-uniform scale in Manual Editing mode
- **WHEN** the user hovers over the visible cone for a support point
- **THEN** the `pin_sphere` centre coincides with the visible cone's anchor
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
