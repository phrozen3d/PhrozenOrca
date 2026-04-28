## ADDED Requirements

### Requirement: "Add Overhang Supports" button generates island support points
The existing "Add Overhang Supports" button in `GLGizmoLcdOverhangDetection` SHALL generate support points for all currently detected islands and inject them into the manual support point list. No new buttons SHALL be created.

#### Scenario: After Detect Selected — supports only current model
- **WHEN** the user ran "Detect Selected" on model A and then clicks "Add Overhang Supports"
- **THEN** `uniform_support_island()` SHALL be called for every island in `m_overhang_area_index_map` (which covers only model A's islands)
- **THEN** support points SHALL be injected into model A's `sla_support_points` only
- **THEN** model A's `slaposSupportTree` SHALL be invalidated and re-run

#### Scenario: After Detect All — supports all models
- **WHEN** the user ran "Detect All" and then clicks "Add Overhang Supports"
- **THEN** `uniform_support_island()` SHALL be called for every island across all objects in `m_overhang_area_index_map`
- **THEN** each object's support points SHALL be updated separately, and each object's `slaposSupportTree` SHALL be re-run

#### Scenario: Button disabled when no island data
- **WHEN** `m_overhang_area_index_map` is empty
- **THEN** the "Add Overhang Supports" button SHALL be in disabled state

### Requirement: Targeted support injection is idempotent
Clicking Add Support multiple times for the same island SHALL NOT accumulate duplicate support points.

#### Scenario: Repeat click replaces previous island support points
- **WHEN** island support points have already been generated for island #N
- **WHEN** the user clicks "Add Support" for island #N again
- **THEN** the previously injected `SupportPointType::island` points at that island's Z level SHALL be removed first
- **THEN** a fresh set of points SHALL be generated and injected
- **THEN** the total support point count SHALL NOT increase unboundedly with repeated clicks

### Requirement: Targeted support does not affect existing manual points
Support points manually placed by the user SHALL remain unaffected when island support generation is triggered.

#### Scenario: Manual points preserved after targeted support
- **WHEN** the user has manually placed support points at various locations
- **WHEN** the user clicks "Add Support for All Islands"
- **THEN** all pre-existing manually placed points SHALL remain in `sla_support_points`
- **THEN** only the newly injected island points (type `SupportPointType::island`) SHALL be added

### Requirement: Targeted support respects FDM guard
Island support generation SHALL only be available when the active printer uses SLA technology.

#### Scenario: Buttons absent for FDM printers
- **WHEN** the active printer is FDM (not SLA)
- **THEN** the island support generation buttons SHALL NOT be rendered or SHALL be fully disabled
- **THEN** no island-related rendering SHALL occur
