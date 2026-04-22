## ADDED Requirements

### Requirement: Island overlay auto-shows on Gizmo activation
When the SLA Support Gizmo transitions to active state (`on_set_state(On)`), the island overlay SHALL automatically become visible if valid island contour data exists, without requiring any user action.

#### Scenario: Overlay appears immediately on Gizmo enter with data
- **WHEN** the user enters the SLA Support Gizmo
- **AND** `SLAPrintObject::island_contours().valid == true`
- **THEN** the island contour overlay SHALL render in the 3D view immediately, without any button press or toggle

#### Scenario: No overlay on Gizmo enter without data
- **WHEN** the user enters the SLA Support Gizmo
- **AND** `island_contours().valid == false` (auto-generate not yet run)
- **THEN** no island overlay SHALL render
- **THEN** the overlay SHALL appear automatically once auto-generate completes

### Requirement: Island overlay auto-hides on Gizmo deactivation
When the SLA Support Gizmo transitions to inactive state (`on_set_state(Off)`), the island overlay SHALL immediately stop rendering and all associated GL resources SHALL be released.

#### Scenario: Overlay disappears on Gizmo exit
- **WHEN** the user exits the SLA Support Gizmo (switches to another Gizmo or normal mode)
- **THEN** island contour polygons SHALL immediately stop rendering
- **THEN** `m_island_overlay_model` and `m_island_highlight_model` SHALL be reset
- **THEN** no island geometry SHALL remain visible in the 3D view

#### Scenario: Re-entering Gizmo restores overlay
- **WHEN** the user exits the Gizmo and then re-enters it
- **AND** island contour data was valid when the Gizmo was exited
- **THEN** `sync_island_data()` SHALL be called on re-entry
- **THEN** the overlay SHALL be rebuilt and rendered automatically

### Requirement: Island count displayed in panel
The Gizmo panel SHALL display the current number of detected islands when data is available, and a descriptive empty state when data is unavailable.

#### Scenario: Count shown when data valid
- **WHEN** `island_contours().valid == true` and `islands` is non-empty
- **THEN** the panel SHALL display the total island count (e.g., "Islands: 5")

#### Scenario: Empty state shown before auto-generate
- **WHEN** `island_contours().valid == false` or `islands` is empty
- **THEN** the panel SHALL display a descriptive message (e.g., "No islands (run Auto-Generate first)")
- **THEN** the Add Support buttons SHALL be disabled
