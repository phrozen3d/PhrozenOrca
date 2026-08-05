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

### Requirement: Preview cone shape and shading do not depend on support point type

The mesh construction function and shader used to render a Points-preview cone SHALL be the same regardless of `support_point.type`. Manual (`manual_add`) points and auto-generated (`island` / `slope`) points SHALL use the same pinhead mesh construction (`pinhead()`) and the same lit shader (`gouraud_light`).

`render_points()` SHALL NOT select a different mesh-building function or a different shader based on whether a point is `manual_add`.

#### Scenario: Manual point uses the same mesh shape as auto points

- **GIVEN** a `manual_add` support point and an auto-generated support point with identical size parameters
- **WHEN** both are rendered in Points preview
- **THEN** both cones have the same geometry (rounded back-sphere tip via the tangent-circle robe), not the simplified flat-disc shape

#### Scenario: Manual point is lit like auto points

- **GIVEN** a `manual_add` support point rendered in Points preview
- **WHEN** the user rotates the camera around it
- **THEN** the cone's shading changes with view angle (directional lighting), the same as an auto-generated point
- **AND** the cone is NOT rendered with a flat, view-independent uniform color

#### Scenario: Slicing output is unaffected

- **GIVEN** any support point, manual or auto-generated
- **WHEN** the object is sliced
- **THEN** the generated support geometry is identical to the behavior before this change
- **AND** this requirement governs only the Points-preview rendering path, not `SupportTreeBuildsteps`

### Requirement: Support point type coloring applies regardless of editing mode

The color differentiation by `support_point.type` (`manual_add` → CYAN, `island` → ORANGE, other → LIGHT_GRAY) SHALL apply whether or not `m_editing_mode` is true. Non-editing mode (Points view) SHALL NOT collapse all points to a single uniform color.

Interaction-driven colors (hover → CYAN, selected → REDISH) SHALL remain gated on `m_editing_mode`, since hover and selection have no meaning outside editing mode.

The locked-island indicator (BLUEISH, gated on `m_lock_unique_islands`) SHALL remain gated on `m_editing_mode` — locking is an editing-mode-only operation and has no corresponding state to display outside editing mode.

#### Scenario: Points view shows type-differentiated colors

- **GIVEN** Points view (non-editing mode) with a mix of auto-generated island points, auto-generated slope points, and manual points
- **WHEN** the gizmo renders them
- **THEN** manual points are CYAN, island points are ORANGE, slope points are LIGHT_GRAY
- **AND** the points are NOT all rendered in the same uniform gray

#### Scenario: Locked-island indicator stays editing-mode-only

- **GIVEN** `m_lock_unique_islands` is enabled and an island point exists
- **WHEN** the gizmo renders in Points view (non-editing mode)
- **THEN** the island point is rendered ORANGE (the type-based color), not BLUEISH
- **AND** BLUEISH is only used when `m_editing_mode` is true

#### Scenario: Hover and selection colors unaffected

- **GIVEN** Manual editing mode with a hovered point and a separately selected point
- **WHEN** the gizmo renders them
- **THEN** the hovered point is CYAN and the selected point is REDISH, matching the behavior before this change

### Requirement: Preview geometry cache is unaffected by shape/shading unification

The per-point geometry cache (`m_head_model_cache`, keyed by `HeadGeomKey`) SHALL continue to avoid rebuilding `GLModel` instances every frame after this change. Points with identical size parameters SHALL share a cache entry regardless of `support_point.type`.

#### Scenario: Auto and manual points with matching size share one cache entry

- **GIVEN** an auto-generated point and a manual point with identical head/pillar/contact dimensions
- **WHEN** both are rendered in the same frame
- **THEN** they resolve to the same `HeadGeomKey` cache entry
- **AND** `GLModel::init_from()` is called at most once for that entry, not once per point

#### Scenario: Steady-state frame cost is unchanged

- **GIVEN** a scene with several hundred support points already rendered once (cache warmed)
- **WHEN** the camera is rotated across further frames with no parameter changes
- **THEN** `GLModel::init_from()` is called 0 times per frame, matching the behavior established by `perf-sla-support-points-preview-render`

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

