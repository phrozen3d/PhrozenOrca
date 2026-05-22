# Spec: sla-orient-job-config-safety

## Purpose

Ensure `OrientJob` reads the correct overhang angle configuration option based on printer technology, preventing a nullptr dereference crash when running the orient operation on SLA printers.

## Requirements

### Requirement: OrientJob reads printer-technology-appropriate overhang angle config
`OrientJob::get_orient_mesh()` SHALL select the overhang angle config option based on the active printer technology. When the printer technology is SLA, it SHALL read `support_critical_angle` (coFloat, degrees). When the printer technology is FFF, it SHALL read `support_threshold_angle` (coInt, degrees). The assigned value SHALL be in degrees with no additional unit conversion, as `OrientMesh::overhang_angle` stores degrees.

#### Scenario: SLA printer — global config provides support_critical_angle
- **WHEN** `printer_technology() == ptSLA`
- **AND** `obj->config` does not contain `support_critical_angle`
- **AND** `full_config()` contains `support_critical_angle`
- **THEN** `om.overhang_angle` SHALL be set to `full_config().opt_float("support_critical_angle")` without any degree-to-radian conversion

#### Scenario: SLA printer — support_critical_angle absent from config
- **WHEN** `printer_technology() == ptSLA`
- **AND** neither `obj->config` nor `full_config()` contains `support_critical_angle`
- **THEN** `om.overhang_angle` SHALL retain the `OrientMesh` default value (30 degrees) and the function SHALL NOT crash

#### Scenario: FFF printer — behavior unchanged
- **WHEN** `printer_technology() != ptSLA`
- **THEN** `OrientJob::get_orient_mesh()` SHALL read `support_threshold_angle` using the original logic (object config override → full_config fallback), identical to the pre-fix behavior

#### Scenario: SLA printer — opt_int("support_threshold_angle") is never called
- **WHEN** `printer_technology() == ptSLA`
- **THEN** `opt_int("support_threshold_angle")` SHALL NOT be called on any config object, eliminating the nullptr dereference crash
