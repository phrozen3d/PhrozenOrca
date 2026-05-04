## ADDED Requirements

### Requirement: Island detection supports Low / Medium / High accuracy levels
The system SHALL provide three island detection accuracy levels, each using a distinct layer height for the temporary re-slice used in island detection. The actual print layer height SHALL NOT be modified.

| Level  | Detection Layer Height |
|--------|----------------------|
| High   | 0.05 mm              |
| Medium | 0.1 mm               |
| Low    | 0.5 mm               |

#### Scenario: High accuracy detects fine islands
- **WHEN** the user selects High accuracy and clicks Detect Selected
- **THEN** the system SHALL re-slice the CSG mesh at 0.05 mm layer height for detection
- **THEN** `po.m_model_slices` SHALL remain unchanged (still at the print layer height)
- **THEN** small islands that would be missed at 0.1 mm or 0.5 mm SHALL be included in the result

#### Scenario: Low accuracy produces larger island areas
- **WHEN** the user selects Low accuracy and clicks Detect Selected
- **THEN** the system SHALL re-slice the CSG mesh at 0.5 mm layer height for detection
- **THEN** island cross-sections captured at the coarser 0.5 mm Z grid MAY be larger than those at 0.05 mm
- **THEN** the number of detected islands MAY be lower than High accuracy

#### Scenario: Detection layer height does not affect print output
- **WHEN** the user changes the accuracy level to any value
- **THEN** `po.m_model_slices`, `po.m_support_point_generator_data`, and the SL1/ZIP slice output SHALL be identical to the state before the level change
- **THEN** auto generate support points SHALL use the original `m_support_point_generator_data` and SHALL NOT be affected

### Requirement: Temporary detection data is discarded after extraction
The re-sliced data produced for island detection SHALL be local to the detection function and SHALL NOT be stored in any `SLAPrintObject` field other than `m_island_contours`.

#### Scenario: No persistent detection slices
- **WHEN** Detect Selected completes at any accuracy level
- **THEN** the temporary `SupportPointGeneratorData` built from the detection slices SHALL be destroyed
- **THEN** `po.m_mesh_to_slice` SHALL remain unmodified
- **THEN** `po.m_model_height_levels` SHALL remain unmodified

### Requirement: Changing accuracy level requires re-detection to take effect
Switching the accuracy level SHALL NOT automatically re-run island detection. The updated level SHALL be applied on the next Detect Selected invocation.

#### Scenario: Accuracy change deferred until re-detect
- **WHEN** the user changes the accuracy level from High to Low
- **THEN** the currently displayed island overlay SHALL remain unchanged
- **WHEN** the user clicks Detect Selected
- **THEN** the system SHALL re-detect using the new Low (0.5 mm) layer height
- **THEN** the overlay SHALL update to reflect the new detection result
