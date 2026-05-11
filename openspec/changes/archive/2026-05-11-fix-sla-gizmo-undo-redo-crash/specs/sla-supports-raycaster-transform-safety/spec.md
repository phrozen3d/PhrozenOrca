## ADDED Requirements

### Requirement: SlaSupports raycaster transform update skips null volume
`GLGizmoSlaSupports::update_point_raycasters_for_picking_transform()` SHALL check that `selection.get_first_volume()` is non-null before dereferencing. If null, the function SHALL return immediately without updating transforms. The raycaster transforms SHALL be updated on the next frame when selection is available.

#### Scenario: get_first_volume() returns null during undo restore
- **WHEN** `update_point_raycasters_for_picking_transform()` is called from `data_changed(is_serializing=true)`
- **AND** `selection.get_first_volume()` returns null (selection not yet synchronized after undo)
- **THEN** the function SHALL return without crash and without modifying any raycaster transforms

#### Scenario: get_first_volume() returns null during register path
- **WHEN** `update_point_raycasters_for_picking_transform()` is called from `register_point_raycasters_for_picking()`
- **AND** `selection.get_first_volume()` returns null
- **THEN** the function SHALL return without crash

#### Scenario: get_first_volume() is valid — normal transform update proceeds
- **WHEN** `update_point_raycasters_for_picking_transform()` is called with a valid non-null GLVolume
- **THEN** raycaster transforms SHALL be updated as before (no behavior change on the normal path)

#### Scenario: transforms are corrected on next frame
- **WHEN** `update_point_raycasters_for_picking_transform()` returned early (null volume) in a prior frame
- **AND** the next `data_changed()` call has a valid `get_first_volume()`
- **THEN** raycaster transforms SHALL be updated correctly, and support point picking SHALL work normally