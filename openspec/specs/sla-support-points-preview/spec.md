# sla-support-points-preview Specification

## Purpose
TBD - created by archiving change fix-sla-support-points-preview-mode-gate. Update Purpose after archive.
## Requirements
### Requirement: Structure view hides Points preview cones outside editing

When `m_show_support_structure == true` and `m_editing_mode == false`, `render_points()` SHALL NOT render any Points-preview cone, regardless of whether `m_normal_cache` is empty.

#### Scenario: Auto-generated points, user switches to Structure view

- **GIVEN** the SLA Support gizmo is open in non-editing mode
- **AND** support points have been auto-generated so `m_normal_cache` is non-empty
- **WHEN** the user clicks the "Structure" view button
- **THEN** no Points-preview cone is rendered
- **AND** only the support tree and pad mesh produced by `render_volumes()` are visible

#### Scenario: Overhang Detection triggers Structure view

- **GIVEN** the SLA Support gizmo is open in non-editing mode
- **WHEN** `GLGizmoLcdOverhangDetection` calls `GLGizmoSlaSupports::activate_structure_view()`
- **THEN** no Points-preview cone is rendered after the switch
- **AND** `m_normal_cache` is NOT cleared (the points are preserved so the user can switch back to Points view)

### Requirement: Points view restores Points preview rendering

When `m_show_support_structure == false`, `render_points()` SHALL render Points-preview cones for every entry in the active cache (`m_editing_cache` if `m_editing_mode == true`, otherwise `m_normal_cache`), subject to the existing per-point clipping check.

#### Scenario: User switches back to Points view

- **GIVEN** the gizmo is in Structure view with `m_normal_cache` non-empty
- **WHEN** the user clicks the "Points" view button
- **THEN** all Points-preview cones reappear in the same positions they had before the Structure switch

### Requirement: Editing mode overrides the view-mode gate

When `m_editing_mode == true`, `render_points()` SHALL continue to render Points-preview cones regardless of `m_show_support_structure`, so manual selection, drag, and right-click delete remain functional.

#### Scenario: User toggles Structure during Manual editing

- **GIVEN** the user has entered Manual Editing mode and added or selected support points
- **WHEN** the user clicks the "Structure" view button without leaving editing mode
- **THEN** Points-preview cones for `m_editing_cache` continue to render
- **AND** the user can still hover, click, drag, and right-click them

### Requirement: View-mode toggle synchronises Points-preview picking

When the view-mode toggle changes outside editing mode, the picking raycasters for Points-preview cones SHALL be unregistered when entering Structure view and registered when entering Points view. The toggle handler SHALL also request a viewport redraw.

#### Scenario: Switch to Structure releases picking on cone locations

- **GIVEN** the gizmo is in non-editing Points view with auto-generated points
- **WHEN** the user clicks the "Structure" view button
- **THEN** `unregister_point_raycasters_for_picking()` is invoked
- **AND** the viewport is marked dirty
- **AND** subsequent hover at a previous cone location does not produce any hover highlight from the gizmo, and clicks fall through to the Structure mesh

#### Scenario: Switch to Points restores picking

- **GIVEN** the gizmo is in non-editing Structure view
- **WHEN** the user clicks the "Points" view button
- **THEN** `register_point_raycasters_for_picking()` is invoked
- **AND** the viewport is marked dirty
- **AND** the cones become hoverable and clickable in the same locations as before

#### Scenario: Editing mode keeps picking raycasters registered across the toggle

- **GIVEN** the user is in Manual Editing mode with raycasters registered for `m_editing_cache`
- **WHEN** the user clicks the "Structure" view button
- **THEN** the raycasters are NOT unregistered (editing-mode guard)
- **AND** the user can continue manipulating points while the Structure mesh is visible

### Requirement: Preview anchor follows instance scale

The world position of every Points-preview cone SHALL track the scaled visible-mesh surface. Per point, the head anchor used to build the preview ITS SHALL be `instance_scaling_matrix * support_point.pos` (in raw frame), and the render transform SHALL include `T_zshift * T * R * mirror` (the instance transform with positive scale removed) so the per-vertex world position is `T_zshift * T * R * mirror * (S * raw_pos + mm_offset)`.

#### Scenario: Uniform scale = 1.5

- **GIVEN** an instance with uniform scale 1.5 and auto-generated support points
- **WHEN** the gizmo renders in Points view
- **THEN** each preview cone's anchor lies on the visible (scaled) mesh surface
- **AND** no cone is detached from the visible surface in any axis

#### Scenario: Non-uniform scale = (2, 1, 1)

- **GIVEN** an instance with non-uniform scale `(2, 1, 1)`
- **WHEN** the gizmo renders in Points view
- **THEN** each cone's anchor sits on the X-stretched visible mesh
- **AND** the cone axis follows the scaled-mesh visible surface normal (via inverse-transpose of `S` applied to the raw-mesh normal), not the raw-mesh normal

### Requirement: Preview cone / pillar dimensions stay in mm under instance scale

The visible diameter and length of each Points-preview cone — head / pillar / contact-sphere — SHALL reflect the support-parameter mm values and SHALL NOT scale with the instance scale.

#### Scenario: Cone size invariant across scales

- **GIVEN** two instances of the same model, one at scale 1.0 and one at scale 2.0
- **WHEN** both render the same support point in Points view
- **THEN** the two preview cones have the same head diameter and the same length on screen at equal camera zoom (in world mm)
- **AND** the only visible difference between the two is the anchor's world position

#### Scenario: Non-uniform scale does not stretch cone geometry

- **GIVEN** an instance with non-uniform scale `(1, 1, 3)`
- **WHEN** the gizmo renders a Points-preview cone whose anchor is on a top face
- **THEN** the cone's cross-section remains circular (not elongated along Z)
- **AND** its length matches the support-parameter `width_mm`, not `width_mm * 3`

### Requirement: Points-preview picking is consistent with the rendered cone

The picking sphere registered through `update_point_raycasters_for_picking_transform()` SHALL match the rendered cone in both world position and world radius: the sphere centre at `instance_scaling_matrix * sp.pos`, the picking transform excluding positive instance scale (mirror preserved), and the sphere radius equal to `max(head.r_pin_mm, head.r_contact_mm)` in mm regardless of instance scale.

#### Scenario: Hover under uniform scale

- **GIVEN** an instance with uniform scale 1.5 in Manual Editing mode
- **WHEN** the user hovers over the visible cone for a support point
- **THEN** the gizmo reports a hover-id matching that point
- **AND** there is no hover gap or false hit-test region between the cone and the picking sphere

#### Scenario: Hover under non-uniform scale

- **GIVEN** an instance with non-uniform scale in Manual Editing mode
- **WHEN** the user hovers over the visible cone for a support point
- **THEN** the picking sphere centre coincides with the visible cone's anchor
- **AND** the sphere radius matches the cone's head / contact radius in mm

