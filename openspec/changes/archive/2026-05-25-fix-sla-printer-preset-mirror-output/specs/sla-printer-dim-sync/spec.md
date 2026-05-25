## ADDED Requirements

### Requirement: Phrozen SLA machine presets SHALL carry hardware-accurate display dimensions

The three active Phrozen SLA machine preset JSON files SHALL store `display_width`, `display_height`, and `printable_area` values that match the hardware panel specification of each printer model. These values determine raster pixel density, raster_shift (center offset), and the PlatformXLength/PlatformYLength fields in PRZ export.

Required values per preset:

| Preset | display_width (mm) | display_height (mm) | printable_area |
|---|---|---|---|
| Phrozen Sonic Mega 8K S | 330.240 | 185.760 | 330.240 × 185.760 bounding box |
| Phrozen Sonic Mega 8K V2 | 330.240 | 185.760 | 330.240 × 185.760 bounding box |
| Phrozen Sonic Mighty Revo 16K | 211.680 | 118.370 | 211.680 × 118.370 bounding box |

`display_width`, `display_height`, and `printable_area` SHALL remain mutually consistent in each preset, as required by the existing `sla-printer-dim-sync` spec.

#### Scenario: Mega 8K S preset carries correct mm dimensions

- **WHEN** the Phrozen Sonic Mega 8K S machine preset is loaded
- **THEN** `display_width` is `330.240`, `display_height` is `185.760`, and the `printable_area` bounding box is `330.240 mm × 185.760 mm`

#### Scenario: Mega 8K V2 preset carries correct mm dimensions

- **WHEN** the Phrozen Sonic Mega 8K V2 machine preset is loaded
- **THEN** `display_width` is `330.240`, `display_height` is `185.760`, and the `printable_area` bounding box is `330.240 mm × 185.760 mm`

#### Scenario: Mighty Revo 16K preset carries correct mm dimensions

- **WHEN** the Phrozen Sonic Mighty Revo 16K machine preset is loaded
- **THEN** `display_width` is `211.680`, `display_height` is `118.370`, and the `printable_area` bounding box is `211.680 mm × 118.370 mm`

#### Scenario: PRZ export reflects corrected dimensions

- **WHEN** a model is sliced and exported as `.prz` using any of the three target presets
- **THEN** the PRZ header `PlatformXLength` and `PlatformYLength` fields reflect the corrected precise dimensions (accounting for the portrait XY swap in `prz_header()`)
