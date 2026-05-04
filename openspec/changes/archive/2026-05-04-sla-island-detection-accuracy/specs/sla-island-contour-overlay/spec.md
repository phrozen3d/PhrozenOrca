## MODIFIED Requirements

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
