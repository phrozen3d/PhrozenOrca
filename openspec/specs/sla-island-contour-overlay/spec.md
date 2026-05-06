## ADDED Requirements

### Requirement: Island contours extracted after auto-generate
After re-slicing at the selected detection layer height completes, the system SHALL extract all `LayerPart` entries where `prev_parts.empty() == true` from the temporary `SupportPointGeneratorData.layers`, pair each with its `print_z`, and store them as an `IslandContourSet` in `SLAPrintObject`. Island contours with area below the configured minimum threshold SHALL be excluded. The detection layer height is determined by the current `DetectionAccuracy` level (High=0.05mm, Medium=0.1mm, Low=0.5mm) and is independent of the print layer height.

#### Scenario: Contours available after Detect Selected
- **WHEN** the user clicks Detect Selected and the temporary re-slice completes successfully
- **THEN** `SLAPrintObject::island_contours().valid` SHALL be `true`
- **THEN** `island_contours().islands` SHALL contain one entry per detected island with `print_z` and `contour` fields populated
- **THEN** the detection result SHALL reflect the currently selected accuracy level's layer height

#### Scenario: No contours before Detect Selected
- **WHEN** the Gizmo is opened but Detect Selected has not been clicked
- **THEN** `SLAPrintObject::island_contours().valid` SHALL be `false`
- **THEN** no island overlay SHALL be rendered

#### Scenario: Micro-islands excluded
- **WHEN** an island contour has area below the minimum threshold (`minimal_bounding_sphere_radius² × π`)
- **THEN** that island SHALL NOT appear in `island_contours().islands`

#### Scenario: Contours cleared on step invalidation
- **WHEN** the user modifies model geometry or support settings causing `slaposObjectSlice` to be invalidated
- **THEN** `SLAPrintObject::clear_island_contours()` SHALL be called
- **THEN** `island_contours().valid` SHALL be `false` until Detect Selected is re-run

### Requirement: Island overlay rendered as three-layer 3D visualization

When the Island Detection Gizmo is active and island contour data is valid, `GLGizmoLcdOverhangDetection` SHALL render three distinct visual layers at each island location:

1. **Orange flat contour** (`m_island_original_model`): original unscaled island outline, always visible
2. **Yellow extruded solid** (`m_island_overlay_model`): scaled solid for unselected islands, depth-tested
3. **Gray extruded solid** (`m_island_highlight_model`): scaled solid for the selected island, depth-tested

#### Scenario: Three-layer visualization with valid data
- **WHEN** the Island Detection Gizmo is active AND `island_contours().valid == true`
- **THEN** all three visual layers SHALL appear at each island region
- **THEN** the orange flat contour SHALL always be visible regardless of camera angle
- **THEN** the yellow/gray extruded solid side walls SHALL be visible from oblique viewing angles

#### Scenario: Visualization correct under all instance transforms
- **WHEN** the model instance has scale, rotation (X/Y/Z tilt), Z-axis rotation, translation, or any combination thereof
- **THEN** all three visual layers SHALL appear at the world-space position matching the rendered model
- **THEN** the coordinate conversion SHALL apply `correction = instances[0]->get_matrix() * po.trafo().inverse()` to convert print-space contour points to world space
- **THEN** the flat original contour SHALL share the same world-Z reference as the extruded solid top face (derived from centroid to avoid XY→Z coupling artifacts)

#### Scenario: Render passes and GL state
- **WHEN** island contour meshes are rendered
- **THEN** Pass 1 (extruded solids) SHALL use `gouraud_light` shader with `GL_DEPTH_TEST` ON and `GL_CULL_FACE` ON
- **THEN** Pass 2 (original flat) SHALL use `flat` shader with `GL_DEPTH_TEST` OFF
- **THEN** `GL_BLEND` SHALL be enabled for both passes

#### Scenario: Overlay uses island colors
- **WHEN** island contour meshes are rendered
- **THEN** unselected island extruded solid color SHALL be `island_overlay_color()` (bright yellow, alpha 0.4–0.6)
- **THEN** selected island extruded solid color SHALL be `island_selected_color()` (bright gray, alpha 0.65)
- **THEN** original flat contour color SHALL be `island_contour_color()` (bright orange, alpha 0.75)

#### Scenario: Overlay top face Z offset
- **WHEN** island overlay meshes are built
- **THEN** the top face SHALL be at `print_z + m_island_overlay_z_offset` (default: `print_z - 0.05mm`)
- **THEN** the bottom face SHALL be at `print_z + m_island_overlay_z_offset - m_island_overlay_thickness`

#### Scenario: Selected island excluded from yellow overlay
- **WHEN** an island is selected
- **THEN** the selected island SHALL be excluded from `m_island_overlay_model` (yellow)
- **THEN** the selected island SHALL appear only in `m_island_highlight_model` (gray)
- **THEN** switching selection SHALL trigger both `rebuild_island_overlay_mesh()` and `rebuild_island_highlight_mesh()`

#### Scenario: Overlay absent when Gizmo is inactive
- **WHEN** the user exits the Island Detection Gizmo
- **THEN** all three island visualization models SHALL be reset
- **THEN** no island overlay meshes SHALL be rendered

---

### Requirement: No sphere cursor or painting in Island Detection mode

The Island Detection Gizmo SHALL NOT render a sphere cursor when the mouse hovers over the model. The gizmo SHALL NOT provide any face-painting or selection-painting functionality inherited from `GLGizmoPainterBase`.

#### Scenario: No sphere cursor
- **WHEN** the Island Detection Gizmo is active and the mouse is over the model surface
- **THEN** no sphere cursor SHALL be rendered
- **THEN** `render_cursor()` SHALL NOT be called from `render_painter_gizmo()`
- **THEN** `update_raycast_cache()` SHALL still be called each frame to keep `m_rr` current

#### Scenario: No paint on click
- **WHEN** the user left-clicks or right-clicks while in Island Detection mode
- **THEN** no mesh face coloring SHALL occur
- **THEN** `gizmo_event()` SHALL NOT be triggered by mouse button events

---

### Requirement: 3D view navigation in Island Detection mode

While in Island Detection mode, mouse left-drag SHALL orbit the 3D view and mouse right-drag SHALL pan, with the same feel as normal canvas navigation.

#### Scenario: Left-drag orbits around surface hit point
- **WHEN** the user left-drags and the mouse was over the model surface at drag start
- **THEN** the camera SHALL orbit around the surface contact point (world-space hit point from `m_rr`)
- **THEN** the rotation formula SHALL be identical to GLCanvas3D: `rot = Vec3d(Δx, Δy, 0) * (π × 0.8 / 180°) * orbit_mult`
- **THEN** `camera.rotate_on_sphere_with_target(rot.x(), rot.y(), false, hit_point)` SHALL be called each drag frame

#### Scenario: Left-drag orbits around selection center when off model
- **WHEN** the user left-drags and the mouse was NOT over the model surface at drag start
- **THEN** the camera SHALL orbit around `selection.get_bounding_box().center()`

#### Scenario: Right-drag pans
- **WHEN** the user right-drags
- **THEN** `on_mouse()` SHALL return `false` for RightDown / RightDrag / RightUp
- **THEN** the canvas SHALL handle pan as in normal navigation mode

### Requirement: Overlay updates on re-generate
When auto-generate is re-run, the island overlay SHALL reflect the new result.

#### Scenario: Overlay refreshes after re-generate
- **WHEN** the user modifies support density and re-runs auto-generate
- **THEN** the previous island overlay SHALL be cleared
- **THEN** a new overlay SHALL be built from the updated `IslandContourSet`
- **THEN** the overlay SHALL update without requiring the Gizmo to be closed and re-opened
