## ADDED Requirements

### Requirement: Island contours extracted after auto-generate
After `slaposSupportPoints` completes, the system SHALL extract all `LayerPart` entries where `prev_parts.empty() == true` from `SupportPointGeneratorData.layers`, pair each with its `print_z`, and store them as an `IslandContourSet` in `SLAPrintObject`. Island contours with area below the configured minimum threshold SHALL be excluded.

#### Scenario: Contours available after auto-generate
- **WHEN** the user runs auto-generate support points and `slaposSupportPoints` completes successfully
- **THEN** `SLAPrintObject::island_contours().valid` SHALL be `true`
- **THEN** `island_contours().islands` SHALL contain one entry per detected island with `print_z` and `contour` fields populated

#### Scenario: No contours before auto-generate
- **WHEN** the Gizmo is opened but auto-generate has not been run (only manual points exist)
- **THEN** `SLAPrintObject::island_contours().valid` SHALL be `false`
- **THEN** no island overlay SHALL be rendered

#### Scenario: Micro-islands excluded
- **WHEN** an island contour has area below the minimum threshold (`minimal_bounding_sphere_radius² × π`)
- **THEN** that island SHALL NOT appear in `island_contours().islands`

#### Scenario: Contours cleared on step invalidation
- **WHEN** the user modifies model geometry or support settings causing `slaposSupportPoints` to be invalidated
- **THEN** `SLAPrintObject::clear_island_contours()` SHALL be called
- **THEN** `island_contours().valid` SHALL be `false` until auto-generate is re-run

### Requirement: Island overlay rendered in 3D view
When the SLA Support Gizmo is active and island contour data is valid, `GLGizmoSlaSupports` SHALL render a semi-transparent colored polygon overlay on the model surface at each island location.

#### Scenario: Overlay visible with valid data
- **WHEN** the SLA Support Gizmo is active AND `island_contours().valid == true`
- **THEN** colored semi-transparent polygons SHALL appear on the model surface covering each island region

#### Scenario: Overlay uses island color at 40% alpha
- **WHEN** island contour polygons are rendered
- **THEN** the color SHALL be `ColorRGBA(1.0f, 0.85f, 0.2f, 0.4f)` (bright yellow-orange, alpha 40%)
- **THEN** `GL_DEPTH_TEST` SHALL be disabled so the overlay is always visible regardless of viewing angle

#### Scenario: Overlay uses Z offset to avoid Z-fighting
- **WHEN** island contour meshes are built via `triangulate_expolygon_3d()`
- **THEN** each polygon SHALL be placed at `print_z + 0.05f` mm above the layer surface

#### Scenario: Overlay absent when Gizmo is inactive
- **WHEN** the user exits the SLA Support Gizmo
- **THEN** no island overlay polygons SHALL be rendered

### Requirement: Overlay updates on re-generate
When auto-generate is re-run, the island overlay SHALL reflect the new result.

#### Scenario: Overlay refreshes after re-generate
- **WHEN** the user modifies support density and re-runs auto-generate
- **THEN** the previous island overlay SHALL be cleared
- **THEN** a new overlay SHALL be built from the updated `IslandContourSet`
- **THEN** the overlay SHALL update without requiring the Gizmo to be closed and re-opened
