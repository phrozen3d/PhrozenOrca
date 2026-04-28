## ADDED Requirements

### Requirement: Island navigation count reflects current detection scope
The "Overhang Area" navigation widget (`< [current / total] >`) SHALL source its total from `m_overhang_area_index_map.size()`, which covers either the current model's islands (after Detect Selected) or all models' islands combined (after Detect All).

#### Scenario: Count after Detect Selected
- **WHEN** the user clicks "Detect Selected" for model A
- **THEN** `m_total_overhang_areas` SHALL equal model A's `island_contours().islands.size()`
- **THEN** `m_overhang_area_index_map` SHALL contain only entries with `obj_idx == A`

#### Scenario: Count after Detect All
- **WHEN** the user clicks "Detect All"
- **THEN** `m_total_overhang_areas` SHALL equal the sum of island counts across all SLA objects
- **THEN** `m_overhang_area_index_map` SHALL contain entries for all objects in scene order

#### Scenario: Count resets when no data available
- **WHEN** no detection has been run (all `IslandContourSet.valid == false`)
- **THEN** `m_total_overhang_areas` SHALL be 0 and navigation buttons SHALL be no-ops

### Requirement: On Gizmo enter, auto-focus on the pre-selected model
When the user enters the Gizmo with a scene object already selected, the Gizmo SHALL initialize `m_current_model_index` to match the selected object and focus the camera on that model's first island if data exists.

#### Scenario: Pre-selected model focused on entry
- **WHEN** the user selects object B in the scene and then enters the Gizmo
- **THEN** `m_current_model_index` SHALL be set to the index of object B
- **THEN** the model name display SHALL show object B's name
- **THEN** if object B has valid island data, the camera SHALL focus on its first island

#### Scenario: Model navigation updates overhang area scope
- **WHEN** the user clicks `<` or `>` in the model navigation
- **THEN** `rebuild_overhang_area_index_map(false)` SHALL be called for the new current model
- **THEN** `m_total_overhang_areas` SHALL update to reflect only the new model's islands
- **THEN** `m_current_overhang_area_index` SHALL reset to 0

### Requirement: Navigation buttons trigger Camera Focus
When the user clicks `<` or `>` in the existing "Overhang Area" navigation, the camera SHALL automatically move to frame the newly selected island.

#### Scenario: Right arrow focuses next island
- **WHEN** the user clicks `>` and `m_current_overhang_area_index` increments
- **THEN** `focus_camera_on_island(m_current_overhang_area_index)` SHALL be called immediately after the index update
- **THEN** the camera SHALL move and zoom so the selected island fills approximately 70–90% of the viewport

#### Scenario: Left arrow focuses previous island
- **WHEN** the user clicks `<` and `m_current_overhang_area_index` decrements
- **THEN** `focus_camera_on_island(m_current_overhang_area_index)` SHALL be called immediately after the index update
- **THEN** the camera SHALL focus on the newly selected island

#### Scenario: No focus call when at boundary
- **WHEN** the user clicks `<` while already at index 0, or `>` while at the last index
- **THEN** the index SHALL NOT change (existing boundary guard)
- **THEN** `focus_camera_on_island()` SHALL NOT be called

### Requirement: Camera focus uses island bounding box
The camera focus calculation SHALL derive a 3D bounding box from the selected island's ExPolygon contour, then use `GLCanvas3D::zoom_to_box()` to frame it.

#### Scenario: Focus correctly transforms coordinates
- **WHEN** `focus_camera_on_island(idx)` is called
- **THEN** `ExPolygon::Point` values SHALL be converted from scaled integer coordinates via `unscale()` before bounding box calculation
- **THEN** the Z range SHALL be `[print_z - 2.0, print_z + 2.0]` mm
- **THEN** the resulting `BoundingBoxf3` SHALL be transformed by `po->trafo()` and passed to `m_parent.zoom_to_box()`

### Requirement: Selected island highlighted in overlay
When the navigation index changes, the currently selected island SHALL render with a distinct highlight color to distinguish it from other islands.

#### Scenario: Current island shown in highlight color
- **WHEN** `m_current_overhang_area_index` points to island N
- **THEN** island N's overlay polygon SHALL render with `ColorRGBA(1.0f, 0.5f, 0.0f, 0.75f)` (bright orange, alpha 75%) at Z offset +0.10 mm
- **THEN** all other island overlays SHALL render at normal color `ColorRGBA(1.0f, 0.85f, 0.2f, 0.4f)` (alpha 40%)
