## Why

Three active Phrozen SLA machine presets carried inaccurate display dimensions and an incomplete mirror-mode model, causing `.prz` exports to write incorrect X/Y mirror bytes and preventing the Printer Settings dialog from round-tripping a third mirror mode (DLP_normal). The bugs were silent: the UI looked plausible but the rasterised output was wrong for Normal and DLP-type printers.

## What Changes

- **Dimension correction** — `display_width`, `display_height`, and `printable_area` in the three active Phrozen machine presets are updated to their precise mm values (330.240 × 185.760 for Mega 8K S / V2; 211.680 × 118.370 for Mighty Revo 16K).
- **New `display_mirror_mode` enum config key** — added to `SLAPrinterConfig` with values `normal`, `lcd_mirror`, `dlp_normal`. This key preserves the UI-selected mode across save/load, independent of the two raw mirror booleans.
- **Printer Settings mirror dropdown** — extended from two items (LCD_mirror, DLP_mirror) to three (Normal, LCD_mirror, DLP_normal). DLP_mirror name corrected to DLP_normal.
- **Orientation-aware mirror bool write-back** — `apply_mirror_mode()` now derives `display_mirror_x` from the selected mode *and* the printer's `display_orientation`, because `RasterBase::Trafo` inverts X for Portrait printers. The written value is always the config value that produces the correct final raster mirror, not a hardcoded per-mode bool.
- **Machine preset boolean format fix** — `display_mirror_x/y` values in the three JSON presets changed from `"true"`/`"false"` (rejected by `ConfigOptionBool::deserialize()`) to `"1"`/`"0"`.
- **Legacy preset fallback** — `mirror_mode_from_config()` updated to compute the *final* X mirror from config bool + orientation before classifying old presets, so landscape and portrait presets both decode correctly without a migration.

## Capabilities

### New Capabilities
- `sla-printer-preset-mirror-output`: SLA Printer Settings exposes three named mirror modes; mode identity round-trips via `display_mirror_mode`; `.prz` X/Y mirror bytes are derived orientation-aware from the stored config values; three Phrozen machine presets carry correct dimensions and mirror config.

### Modified Capabilities
- `sla-printer-dim-sync`: Machine preset dimension values for the three target printers are corrected to their precise mm values; the dimension sync behavior described in the existing spec is unchanged, but the starting values in the JSON presets now match hardware spec.

## Impact

- `src/libslic3r/PrintConfig.hpp` / `.cpp` — new `SLAMirrorMode` enum and `display_mirror_mode` config key
- `src/libslic3r/Preset.cpp` — `display_mirror_mode` added to `s_Preset_sla_printer_options` whitelist
- `src/slic3r/GUI/SLAPrinterSettingsDialog.hpp` / `.cpp` — `MirrorMode` enum extended; dropdown, read, and write-back logic updated
- `resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K S.json`
- `resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mega 8K V2.json`
- `resources/profiles/PhrozenSLA/machine/Phrozen Sonic Mighty Revo 16K.json`
- No change to PRZ binary format, rasterization algorithm, or other printer profiles.
