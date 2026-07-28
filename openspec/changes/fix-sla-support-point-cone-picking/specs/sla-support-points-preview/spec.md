## MODIFIED Requirements

### Requirement: Points-preview picking is consistent with the rendered cone

The pickable volume of a Points-preview support point SHALL be the union of its two registered raycasters — the sphere raycaster covering the pin / contact sphere, and the cone raycaster covering the robe segment — and SHALL cover the entire visible pinhead.

The sphere raycaster SHALL be centred at `instance_scaling_matrix * sp.pos` with radius `max(head.r_pin_mm, head.r_contact_mm)` in mm regardless of instance scale.

The cone raycaster SHALL be transformed so the unit cone (`its_make_cone(1.0, 1.0, …)`, base circle at the origin in the `z = 0` plane, apex at `+Z·h`) aligns with the rendered robe: apex at the anchor, base of radius `head.r_back_mm` at the outward end, axis along the surface normal used to orient the rendered cone. It SHALL NOT remain at `Transform3d::Identity()`, and it SHALL NOT be permanently inactive.

Both raycasters SHALL use the same picking transform convention as the render path — positive instance scale excluded, mirror preserved — and SHALL be registered under the same raycaster id so a hit on either reports the same `m_hover_id`.

Both raycasters SHALL follow the same clipping-driven active-state rule: when a point is clipped, neither is active.

The pickable volume SHALL NOT extend beyond the visible pinhead: the cone raycaster is inscribed within the rendered frustum (which widens from `r_pin` to `r_back`), so empty space adjacent to the rendered geometry SHALL NOT register a hit.

#### Scenario: Hover on the exposed cone body

- **GIVEN** support points have been generated and are visible in Points view
- **WHEN** the user hovers over the robe segment of a pinhead, away from its top sphere
- **THEN** the gizmo reports a hover-id matching that point
- **AND** clicking there selects that point

#### Scenario: Hover under uniform scale

- **GIVEN** an instance with uniform scale 1.5 in Manual Editing mode
- **WHEN** the user hovers over the visible cone for a support point
- **THEN** the gizmo reports a hover-id matching that point
- **AND** every part of the visible pinhead — top sphere, robe, back sphere — is hoverable

#### Scenario: Hover under non-uniform scale

- **GIVEN** an instance with non-uniform scale in Manual Editing mode
- **WHEN** the user hovers over the visible cone for a support point
- **THEN** the sphere raycaster centre coincides with the visible cone's anchor
- **AND** the cone raycaster axis follows the same scaled-mesh surface normal that orients the rendered cone
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

#### Scenario: Clipped point disables both raycasters

- **GIVEN** the object clipper is active and a support point is clipped
- **WHEN** the user hovers at that point's former location
- **THEN** neither the sphere nor the cone raycaster reports a hit
