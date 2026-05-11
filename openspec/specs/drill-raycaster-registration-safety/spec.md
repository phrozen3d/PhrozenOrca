## ADDED Requirements

### Requirement: Drill raycaster registration uses Selection to obtain ModelObject
`GLGizmoDrill::register_hole_raycasters_for_picking()` SHALL obtain the ModelObject via `m_parent.get_selection().get_model()->objects[obj_idx]` instead of `m_c->selection_info()->model_object()`. This avoids using a potentially stale or dangling pointer when called during undo/redo restore, before `m_c` has been refreshed. If the Selection is invalid or ModelObject is null, the function SHALL return without registering any raycasters.

#### Scenario: Previous gizmo was a non-SLA gizmo (null case)
- **WHEN** `register_hole_raycasters_for_picking()` is called during `activate_gizmo()` inside `GLGizmosManager::load()`
- **AND** the previous active gizmo did not require SelectionInfo (e.g., Move gizmo)
- **AND** `selection.get_object_idx()` returns -1 or model is unavailable
- **THEN** the function SHALL return without crash and without registering any raycasters

#### Scenario: Previous gizmo was an SLA gizmo (dangling pointer case)
- **WHEN** `register_hole_raycasters_for_picking()` is called during `activate_gizmo()` inside `GLGizmosManager::load()`
- **AND** the previous active gizmo was an SLA gizmo (Hollow/Drill/Support), leaving SelectionInfo valid but pointing to the now-deleted pre-undo ModelObject
- **AND** `m_parent.get_selection().get_model()->objects[idx]` correctly returns the freshly restored ModelObject
- **THEN** the function SHALL use the fresh ModelObject and register raycasters correctly without accessing the dangling pointer

#### Scenario: Normal path — valid selection and non-empty drain holes
- **WHEN** `register_hole_raycasters_for_picking()` is called with a valid selection and non-empty `sla_drain_holes`
- **THEN** raycasters SHALL be registered as before (no behavior change on the normal path)

#### Scenario: data_changed() completes registration after undo
- **WHEN** raycaster registration was skipped due to invalid selection during activate_gizmo()
- **AND** `update_after_undo_redo()` subsequently calls `update_data()` → `data_changed(is_serializing=true)`
- **AND** the selection is now valid and m_c is refreshed
- **THEN** `data_changed()` SHALL call `register_hole_raycasters_for_picking()` (via the `m_hole_raycasters.empty()` path) and complete registration successfully