## 1. Config Infrastructure

- [x] 1.1 Add `SLAMirrorMode` enum (`slammNormal`, `slammLCDMirror`, `slammDLPNormal`) to `PrintConfig.hpp`
- [x] 1.2 Add `CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLAMirrorMode)` declaration to `PrintConfig.hpp`
- [x] 1.3 Add `display_mirror_mode` member to `SLAPrinterConfig` struct in `PrintConfig.hpp`
- [x] 1.4 Add `s_keys_map_SLAMirrorMode` static map (`normal`, `lcd_mirror`, `dlp_normal`) and `CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS` to `PrintConfig.cpp`
- [x] 1.5 Register `display_mirror_mode` config key definition in `PrintConfig.cpp` (coEnum, default `lcd_mirror`)
- [x] 1.6 Add `"display_mirror_mode"` to `s_Preset_sla_printer_options` whitelist in `Preset.cpp`

## 2. UI — Mirror Dropdown and Read Path

- [x] 2.1 Extend `SLAPrinterSettingsDialog::MirrorMode` enum to three values: `Normal`, `LCD`, `DLPNormal`
- [x] 2.2 Replace two-item Mirror dropdown (LCD_mirror, DLP_mirror) with three-item dropdown: Normal, LCD_mirror, DLP_normal
- [x] 2.3 Update `reload_from_preset()` switch to map all three `MirrorMode` values to dropdown index 0/1/2
- [x] 2.4 Update `mirror_mode_from_config()`: prefer `display_mirror_mode` key; orientation-aware bool fallback (`final_x = is_portrait ? !config_x : config_x`); DLP_normal not reconstructed from legacy bools (fallback to Normal)

## 3. UI — Write-back Path

- [x] 3.1 Update `sync_local_to_tab()` selection-to-mode mapping to cover index 0 (Normal), 1 (LCD), 2 (DLPNormal)
- [x] 3.2 Rewrite `apply_mirror_mode()` to write `display_mirror_mode` enum + compute `display_mirror_x` orientation-aware (`config_x = is_portrait ? !target_final_x : target_final_x`) + write `display_mirror_y = false`

## 4. Machine Preset — Dimensions

- [x] 4.1 Update Phrozen Sonic Mega 8K S: `display_width` → `330.240`, `display_height` → `185.760`, `printable_area` → `330.240 × 185.760` bounding box
- [x] 4.2 Update Phrozen Sonic Mega 8K V2: same dimension values as Mega 8K S (`330.240 × 185.760`)
- [x] 4.3 Update Phrozen Sonic Mighty Revo 16K: `display_width` → `211.680`, `display_height` → `118.370`, `printable_area` → `211.680 × 118.370` bounding box

## 5. Machine Preset — Mirror Config Values

- [x] 5.1 Add `display_mirror_mode: "normal"`, `display_mirror_x: "1"`, `display_mirror_y: "0"` to Mega 8K S
- [x] 5.2 Add `display_mirror_mode: "normal"`, `display_mirror_x: "1"`, `display_mirror_y: "0"` to Mega 8K V2
- [x] 5.3 Add `display_mirror_mode: "lcd_mirror"`, `display_mirror_x: "0"`, `display_mirror_y: "0"` to Mighty Revo 16K
- [x] 5.4 Confirm all three JSON files use `"1"`/`"0"` format (not `"true"`/`"false"`) for coBool mirror fields

## 6. Verification — Static Checks

- [x] 6.1 Validate JSON syntax of all three updated machine preset files (no parse errors)
- [x] 6.2 Confirm `git diff --stat` shows exactly 8 modified files, no unrelated preset or source files

## 7. Verification — UI Behavior

- [x] 7.1 Open SLA Printer Settings for Mega 8K S / V2 — Mirror dropdown shows "Normal" (index 0)
- [x] 7.2 Open SLA Printer Settings for Mighty Revo 16K — Mirror dropdown shows "LCD_mirror" (index 1)
- [x] 7.3 Select DLP_normal, save, reopen dialog — dropdown restores "DLP_normal" (not "Normal")
- [x] 7.4 Select Normal, save, reopen dialog — dropdown restores "Normal" (not "DLP_normal")

## 8. Verification — .prz Mirror Output (UVtools)

- [x] 8.1 Slice with Mega 8K S (Normal) → UVtools reports X mirror = false, Y mirror = false
- [x] 8.2 Slice with Mega 8K V2 (Normal) → UVtools reports X mirror = false, Y mirror = false
- [x] 8.3 Slice with Mighty Revo 16K (LCD_mirror) → UVtools reports X mirror = true, Y mirror = false
- [x] 8.4 Slice with a portrait printer set to DLP_normal → UVtools reports X mirror = false, Y mirror = false

## 9. Verification — .prz Platform Dimensions (UVtools)

- [x] 9.1 Export `.prz` with each of the three corrected presets and verify PlatformXLength / PlatformYLength reflect the precise corrected display dimensions, accounting for portrait XY ordering in PRZ metadata (PlatformXLength ← display_height, PlatformYLength ← display_width)
