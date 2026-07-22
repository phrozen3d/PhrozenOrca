## ADDED Requirements

### Requirement: Baseline contact spacing calibrated at reference head

The SLA support island sampling configuration produced by `SampleConfigFactory::create()` SHALL be calibrated so that, at the reference support head diameter of 0.4 mm and support density 100%, the thin-region neighbouring contact-point spacing (`thin_max_distance`) is approximately 4.0 mm (within rounding tolerance), replacing the previous ~5.19 mm baseline.

The calibration factor SHALL be derived from named constants
(`kTargetThinSpacing = 4.0 mm`, `kRefHeadDiameter = 0.4 mm`) as
`k = kTargetThinSpacing / thin_max_distance_unscaled(kRefHeadDiameter)`, and MUST NOT be a
hard-coded magic number, so that changing the underlying Prusa geometry constants keeps the
4 mm invariant at the reference head.

#### Scenario: Thin spacing at reference head and 100% density
- **WHEN** `create(0.4)` is evaluated with density 100% (identity `apply_density`)
- **THEN** `thin_max_distance` SHALL equal approximately 4.0 mm (within µm-level tolerance)

#### Scenario: Calibration factor is derived, not hard-coded
- **WHEN** the underlying constants used to compute `thin_max_distance_unscaled` at the reference head change
- **THEN** the derived factor `k` SHALL still yield ~4.0 mm `thin_max_distance` at 0.4 mm head without manual edit of a literal factor

### Requirement: Self-similar scaling of the geometry chain

The calibration SHALL scale the entire spacing/geometry chain uniformly by the same factor `k`,
preserving all existing ratios between `thin_max_distance`, `thick_inner_max_distance`,
`thick_outline_max_distance`, `thin_max_width`, `thick_min_width`, `min_part_length`,
`maximal_distance_from_outline` and `max_align_distance`. Because every such field is a pure
multiple of `max_length_for_one_support_point` (L1), applying `k` at L1 SHALL cascade to all
of them; the thin:inner:outline proportions MUST remain unchanged from the pre-change values.

#### Scenario: Inner and outline spacing scale proportionally
- **WHEN** `create(0.4)` is evaluated after calibration
- **THEN** `thick_inner_max_distance` SHALL be approximately 5.0 mm and `thick_outline_max_distance` approximately 3.75 mm (the pre-change values 6.49 mm and 4.87 mm each multiplied by `k`)

#### Scenario: Ratios between spacing fields are preserved
- **WHEN** any two spacing fields are compared before and after calibration at the same head diameter
- **THEN** their ratio SHALL be identical (uniform scaling, no ratio drift)

### Requirement: Physical head fields remain unscaled

The calibration MUST NOT scale the physical head-size fields `head_radius` and
`minimal_distance_from_outline` (which equals `head_radius`). These SHALL retain their true
values derived directly from the support head diameter, independent of the calibration factor.

#### Scenario: Head radius unchanged by calibration
- **WHEN** `create(d)` is evaluated for any head diameter `d` before and after calibration
- **THEN** `head_radius` and `minimal_distance_from_outline` SHALL be identical in both cases

### Requirement: Head-diameter coupling preserved

The 4 mm target SHALL be anchored only at the 0.4 mm reference head. For other head diameters
the contact spacing SHALL scale proportionally with the head-area-derived geometry (larger head
→ larger spacing), i.e. the calibration applies a single fixed factor and does not decouple
spacing from head diameter.

#### Scenario: Larger head yields larger spacing
- **WHEN** `create(0.8)` is compared with `create(0.4)` after calibration
- **THEN** the 0.8 mm head `thin_max_distance` SHALL be larger than 4.0 mm (not pinned to 4 mm)

#### Scenario: Smaller head yields smaller spacing
- **WHEN** `create(0.2)` is evaluated after calibration
- **THEN** its `thin_max_distance` SHALL be smaller than 4.0 mm

### Requirement: verify() consistency across the head-diameter domain

After calibration, the internal consistency check `verify()` SHALL pass for the full range of
supported head diameters without triggering its self-healing clamp, because scaling only the
spacing fields (not the physical head fields) keeps every `verify()` inequality satisfied.

#### Scenario: verify passes at extreme head diameters
- **WHEN** `create(d)` is evaluated for `d` in {0.2, 0.4, 0.8} mm after calibration
- **THEN** `verify()` SHALL return without clamping any field (all inequalities hold)

### Requirement: Single source of truth across consumers

The calibrated baseline SHALL apply consistently to every consumer of `create()` — the
production slicing path (`create()` → `apply_density()`), the LCD overhang manual detection
tool, and the `create_default_island_configuration()` default member — with no divergent
per-consumer spacing.

#### Scenario: LCD overhang tool uses the calibrated baseline
- **WHEN** the LCD overhang manual detection tool generates island support points at 0.4 mm head
- **THEN** it SHALL use the ~4 mm calibrated spacing (identical baseline to the production path)

### Requirement: Density semantics unchanged

The calibration MUST NOT alter `apply_density()`. Density 100% SHALL correspond to the new
calibrated baseline, and higher densities SHALL continue to reduce spacing by the existing law
(linear for thin/outline, √-law for inner), preserving continuity of the density control.

#### Scenario: 200% density halves the thin spacing
- **WHEN** density 200% is applied to the calibrated `create(0.4)` configuration
- **THEN** `thin_max_distance` SHALL be approximately 2.0 mm (calibrated 4 mm divided by 2)
