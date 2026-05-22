# Spec: sla-arrange-job-compat

## Purpose

Ensure `ArrangeJob` does not access FFF-only print config or model APIs when running in SLA mode, preventing crashes from null FFF print objects and missing SLA config keys.

## Requirements

### Requirement: ArrangeJob skips FFF print config access in SLA mode
`ArrangeJob` SHALL NOT call `PartPlateList::get_current_fff_print()` when the active printer technology is SLA. All FFF print config–dependent parameter initialization (nozzle clearance, extruder clearance, skirt offset, speed table, extruder params) SHALL be skipped for SLA. The arrange operation SHALL still execute and produce a valid layout for SLA objects.

#### Scenario: SLA printer — init_arrange_params returns without calling get_current_fff_print
- **WHEN** `p->printer_technology() == ptSLA`
- **AND** `init_arrange_params(p)` is called
- **THEN** the function SHALL return an `ArrangeParams` populated from `ArrangeSettings` and `BuildVolume` only
- **AND** `get_current_fff_print()` SHALL NOT be called

#### Scenario: SLA printer — printable_height sourced from BuildVolume
- **WHEN** `printer_technology() == ptSLA`
- **THEN** `params.printable_height` SHALL be set to `p->build_volume().printable_height()`
- **AND** FFF-only clearance fields (`clearance_height_to_rod`, `clearance_height_to_lid`, `clearance_radius`, `nozzle_height`, `object_skirt_offset`) SHALL remain at their default-constructed values (0)

#### Scenario: SLA printer — prepare() skips extruder/speed table initialization
- **WHEN** `m_plater->printer_technology() == ptSLA`
- **AND** `ArrangeJob::prepare()` is called
- **THEN** `Model::setExtruderParams()` and `Model::setPrintSpeedTable()` SHALL NOT be called
- **AND** `get_current_fff_print()` in `prepare()` SHALL NOT be called

#### Scenario: SLA printer — scan_first_layer guard prevents crash
- **WHEN** `ArrangeJob::process()` is called with an SLA full_config
- **AND** `global_config.has("scan_first_layer")` returns false
- **THEN** `global_config.opt_bool("scan_first_layer")` SHALL NOT be called
- **AND** the process SHALL continue without crash

#### Scenario: FFF printer — all original arrange behavior preserved
- **WHEN** `printer_technology() != ptSLA`
- **THEN** `init_arrange_params()` SHALL use `get_current_fff_print()` and `PrintConfig` fields as before
- **AND** `prepare()` SHALL call `setExtruderParams()` and `setPrintSpeedTable()` as before
- **AND** `process()` SHALL evaluate `scan_first_layer` when the key exists in `full_config()`

### Requirement: ArrangeJob SLA path uses settings-based arrange parameters
For user-configurable arrange settings (rotation, distance, multi-material plate, axis alignment), `ArrangeJob` in SLA mode SHALL read from `GLCanvas3D::ArrangeSettings` identically to the FFF path. `is_seq_print` SHALL be `false` for SLA, as SLA does not support sequential-by-object printing.

#### Scenario: SLA printer — user-configured distance and rotation honored
- **WHEN** `printer_technology() == ptSLA`
- **AND** the user has set a non-zero arrange distance or enabled rotation in ArrangeSettings
- **THEN** `params.min_obj_distance` SHALL reflect `scaled(settings.distance)`
- **AND** `params.allow_rotations` SHALL reflect `settings.enable_rotation`

#### Scenario: SLA printer — is_seq_print is false
- **WHEN** `printer_technology() == ptSLA`
- **THEN** `params.is_seq_print` SHALL be `false` regardless of the `ArrangeSettings::is_seq_print` value
