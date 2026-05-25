## ADDED Requirements

### Requirement: Printer Settings SHALL expose three named Mirror modes

`SLAPrinterSettingsDialog` SHALL present exactly three Mirror mode options in its dropdown:

1. **Normal** — no X or Y mirror in rasterised output
2. **LCD_mirror** — X mirror applied in rasterised output, no Y mirror
3. **DLP_normal** — no X or Y mirror in rasterised output (distinct named mode, same raster effect as Normal)

The previously-present "DLP_mirror" option SHALL be replaced by "DLP_normal".

#### Scenario: Mirror dropdown shows three items

- **WHEN** the SLA Printer Settings dialog is opened for any SLA printer
- **THEN** the Mirror dropdown contains exactly three items: "Normal" (index 0), "LCD_mirror" (index 1), "DLP_normal" (index 2)

#### Scenario: Normal is the default selection index

- **WHEN** the dialog is opened for a printer with no stored mirror mode key and no stored mirror booleans
- **THEN** the Mirror dropdown defaults to index 0 ("Normal") or to the mode inferred from the legacy fallback, not to "DLP_mirror"

---

### Requirement: Mirror mode identity SHALL round-trip via `display_mirror_mode`

When the user selects a Mirror mode and saves, the dialog SHALL write `display_mirror_mode` as a named enum value (`normal`, `lcd_mirror`, or `dlp_normal`) to the printer config. On the next load, the dialog SHALL restore the same dropdown selection.

This requirement applies even when two modes have identical final raster behavior (Normal and DLP_normal).

#### Scenario: Normal selection round-trips

- **WHEN** the user selects "Normal" and saves the preset
- **THEN** the saved config contains `display_mirror_mode = "normal"` (not `"dlp_normal"`)
- **AND WHEN** the dialog is reopened for the same preset
- **THEN** the Mirror dropdown shows "Normal" (index 0)

#### Scenario: LCD_mirror selection round-trips

- **WHEN** the user selects "LCD_mirror" and saves
- **THEN** the saved config contains `display_mirror_mode = "lcd_mirror"`
- **AND WHEN** the dialog is reopened
- **THEN** the Mirror dropdown shows "LCD_mirror" (index 1)

#### Scenario: DLP_normal selection round-trips as DLP_normal, not Normal

- **WHEN** the user selects "DLP_normal" and saves
- **THEN** the saved config contains `display_mirror_mode = "dlp_normal"` (distinct from `"normal"`)
- **AND WHEN** the dialog is reopened
- **THEN** the Mirror dropdown shows "DLP_normal" (index 2), not "Normal"

---

### Requirement: `.prz` mirror output SHALL match the mode's intended final raster behavior

The final X/Y mirror effect in the rasterised slice image, and in the `.prz` Xmirror/Ymirror header bytes, SHALL match the mode semantics below, regardless of printer `display_orientation`:

| Mode | Final X mirror | Final Y mirror | PRZ Xmirror byte | PRZ Ymirror byte |
|---|---|---|---|---|
| Normal | false | false | 1 | 0 |
| LCD_mirror | true | false | 0 | 0 |
| DLP_normal | false | false | 1 | 0 |

(PRZ byte encoding: Xmirror = `display_mirror_x ? 0 : 1`; Ymirror = `display_mirror_y ? 1 : 0`)

#### Scenario: Normal mode produces no X mirror in .prz

- **WHEN** slicing with a Portrait printer set to Normal mode
- **THEN** the `.prz` Xmirror byte is `1` (no mirror), Ymirror byte is `0` (no mirror)
- **AND** UVtools inspection reports X mirror = false, Y mirror = false

#### Scenario: LCD_mirror mode produces X mirror in .prz

- **WHEN** slicing with a Portrait printer set to LCD_mirror mode
- **THEN** the `.prz` Xmirror byte is `0` (mirror applied), Ymirror byte is `0`
- **AND** UVtools inspection reports X mirror = true, Y mirror = false

#### Scenario: DLP_normal mode produces no X mirror in .prz

- **WHEN** slicing with a Portrait printer set to DLP_normal mode
- **THEN** the `.prz` Xmirror byte is `1` (no mirror), Ymirror byte is `0`
- **AND** UVtools inspection reports X mirror = false, Y mirror = false

---

### Requirement: Write-back SHALL account for display_orientation when computing display_mirror_x

`apply_mirror_mode()` SHALL derive `display_mirror_x` from the selected mode and the current `display_orientation`. For portrait printers, `RasterBase::Trafo` inverts X (`trafo.mirror_x = !config_x`); for landscape printers it uses `config_x` directly.

The formula SHALL be: `config_x = is_portrait ? !target_final_x : target_final_x`

#### Scenario: Portrait Normal writes display_mirror_x = true

- **WHEN** the user selects Normal and saves for a printer with `display_orientation = portrait`
- **THEN** `display_mirror_x` is written as `true` (producing `trafo.mirror_x = false` = no X mirror)

#### Scenario: Portrait LCD_mirror writes display_mirror_x = false

- **WHEN** the user selects LCD_mirror and saves for a portrait printer
- **THEN** `display_mirror_x` is written as `false` (producing `trafo.mirror_x = true` = X mirror applied)

#### Scenario: Landscape LCD_mirror writes display_mirror_x = true

- **WHEN** the user selects LCD_mirror and saves for a printer with `display_orientation = landscape`
- **THEN** `display_mirror_x` is written as `true` (producing `trafo.mirror_x = true` = X mirror applied)

#### Scenario: Portrait DLP_normal writes display_mirror_x = true (same as Normal)

- **WHEN** the user selects DLP_normal and saves for a portrait printer
- **THEN** `display_mirror_x` is written as `true` (identical config value to Normal, producing no X mirror)

---

### Requirement: Legacy preset fallback SHALL derive display mode from config bool and orientation

When a preset config does not contain `display_mirror_mode`, `mirror_mode_from_config()` SHALL compute the *final* X mirror behavior (`final_x = is_portrait ? !config_x : config_x`) and classify:

- `final_x = true` → LCD_mirror
- `final_x = false` → Normal

DLP_normal SHALL NOT be inferred from legacy booleans. A preset displaying as Normal due to fallback remains functionally correct because Normal and DLP_normal produce identical raster output.

#### Scenario: Legacy portrait preset with config_x = true displays as Normal

- **WHEN** a preset has no `display_mirror_mode` key, `display_orientation = portrait`, and `display_mirror_x = true`
- **THEN** `final_x = !true = false` → the dialog displays "Normal"

#### Scenario: Legacy landscape preset with config_x = true displays as LCD_mirror

- **WHEN** a preset has no `display_mirror_mode` key, `display_orientation = landscape`, and `display_mirror_x = true`
- **THEN** `final_x = true` → the dialog displays "LCD_mirror"

#### Scenario: Preset with display_mirror_mode key bypasses fallback

- **WHEN** a preset contains `display_mirror_mode = "dlp_normal"`
- **THEN** the dialog displays "DLP_normal" regardless of the values of `display_mirror_x` and `display_mirror_y`

---

### Requirement: Target Phrozen SLA machine presets SHALL define the correct default Mirror mode and effective mirror config

The three active Phrozen SLA machine presets SHALL each carry an explicit `display_mirror_mode` value and `display_mirror_x`/`display_mirror_y` values that, under Portrait orientation, produce the correct final `.prz` Xmirror/Ymirror bytes for that printer type:

| Preset | display_mirror_mode | display_mirror_x | display_mirror_y | Expected final .prz output |
|---|---|---|---|---|
| Phrozen Sonic Mega 8K S | `"normal"` | `"1"` | `"0"` | X mirror=false, Y mirror=false |
| Phrozen Sonic Mega 8K V2 | `"normal"` | `"1"` | `"0"` | X mirror=false, Y mirror=false |
| Phrozen Sonic Mighty Revo 16K | `"lcd_mirror"` | `"0"` | `"0"` | X mirror=true, Y mirror=false |

The boolean values are Portrait-compatible: `display_mirror_x = "1"` (true) for Normal produces `trafo.mirror_x = !true = false`; `display_mirror_x = "0"` (false) for LCD_mirror produces `trafo.mirror_x = !false = true`.

#### Scenario: Mega 8K S preset loads as Normal with Portrait-compatible bool values

- **WHEN** the Phrozen Sonic Mega 8K S machine preset is loaded by the slicer
- **THEN** `display_mirror_mode` is `normal`, `display_mirror_x` is `true` (from `"1"`), `display_mirror_y` is `false` (from `"0"`)
- **AND** the SLA Printer Settings dialog displays "Normal" for this preset
- **AND** slicing and exporting `.prz` with this preset yields Xmirror byte = 1 (X mirror=false) and Ymirror byte = 0 (Y mirror=false)

#### Scenario: Mega 8K V2 preset loads as Normal with Portrait-compatible bool values

- **WHEN** the Phrozen Sonic Mega 8K V2 machine preset is loaded by the slicer
- **THEN** `display_mirror_mode` is `normal`, `display_mirror_x` is `true` (from `"1"`), `display_mirror_y` is `false` (from `"0"`)
- **AND** the SLA Printer Settings dialog displays "Normal" for this preset
- **AND** slicing and exporting `.prz` with this preset yields Xmirror byte = 1 (X mirror=false) and Ymirror byte = 0 (Y mirror=false)

#### Scenario: Mighty Revo 16K preset loads as LCD_mirror with Portrait-compatible bool values

- **WHEN** the Phrozen Sonic Mighty Revo 16K machine preset is loaded by the slicer
- **THEN** `display_mirror_mode` is `lcd_mirror`, `display_mirror_x` is `false` (from `"0"`), `display_mirror_y` is `false` (from `"0"`)
- **AND** the SLA Printer Settings dialog displays "LCD_mirror" for this preset
- **AND** slicing and exporting `.prz` with this preset yields Xmirror byte = 0 (X mirror=true) and Ymirror byte = 0 (Y mirror=false)

---

### Requirement: machine preset coBool fields SHALL use "1" / "0" serialization format

Any `display_mirror_x` or `display_mirror_y` value written to a machine preset JSON SHALL use the string `"1"` for true and `"0"` for false. The strings `"true"` and `"false"` SHALL NOT be used for `coBool` fields, as `ConfigOptionBool::deserialize()` does not accept them.

#### Scenario: Preset with "1"/"0" mirror bool loads correctly

- **WHEN** a machine preset JSON contains `"display_mirror_x": "1"`
- **THEN** the runtime config reads `display_mirror_x = true`

#### Scenario: Preset with "true"/"false" silently corrupts mirror value

- **WHEN** a machine preset JSON contains `"display_mirror_x": "true"` (invalid format)
- **THEN** `ConfigOptionBool::deserialize()` fails; the substitution fallback sets the value to `false` regardless, corrupting the intended `true` value
