## Context

`SLAPrinterSettingsDialog` exposes a Mirror mode dropdown, but the original implementation had two problems:

1. Only two items (LCD_mirror, DLP_mirror) instead of the required three (Normal, LCD_mirror, DLP_normal). No `display_mirror_mode` key existed — the mode was inferred at read time from two raw booleans (`display_mirror_x`, `display_mirror_y`), which cannot distinguish Normal from DLP_normal since both use the same final bool pair.

2. `apply_mirror_mode()` wrote hardcoded bool values that were correct only for landscape printers. All current Phrozen printers are portrait, and `RasterBase::Trafo` inverts `display_mirror_x` for portrait orientation:
   ```
   trafo.mirror_x = (orientation == roPortrait) ? !config_x : config_x
   ```
   A value of `config_x=true` produces *no* X mirror in portrait — the opposite of its surface meaning. The original code wrote `true` for LCD_mirror, which silently produced no X mirror in the raster output.

3. The machine preset JSONs additionally used `"true"`/`"false"` string literals for `coBool` fields, which `ConfigOptionBool::deserialize()` rejects, causing both values to silently fall through to `false` via the substitution fallback.

## Goals / Non-Goals

**Goals:**
- Three named mirror modes with stable round-trip identity via a dedicated enum key.
- `.prz` X/Y mirror bytes match the selected mode's intended final raster behavior regardless of printer orientation.
- Existing presets without `display_mirror_mode` continue to display a meaningful mode.
- Three Phrozen machine preset JSONs carry correct hardware dimensions and parser-compatible boolean values.

**Non-Goals:**
- No migration for user presets that stored mode-equivalent booleans under the old convention.
- No full LCD/DLP hardware taxonomy or additional printer support beyond the three target presets.
- No changes to other output format writers (SL1, AnycubicSLA) or the PRZ binary layout.
- No cleanup of unreferenced or legacy duplicate preset files.

## Decisions

### Decision 1 — New `display_mirror_mode` enum key, not inferred from booleans

**Choice:** Add `display_mirror_mode` (coEnum: `normal`/`lcd_mirror`/`dlp_normal`) as the canonical mode key. Retain `display_mirror_x/y` as the actual config values consumed by rasterization and PRZ export.

**Rationale:** Normal and DLP_normal have identical final mirror booleans (`false`/`false`). A round-trip that reconstructs the mode from booleans alone cannot distinguish them. A named key resolves ambiguity permanently.

**Alternative considered:** Add a synthetic `display_dlp_normal` boolean. Rejected — one semantic key is cleaner than two overlapping mechanisms.

---

### Decision 2 — Orientation-aware write-back in `apply_mirror_mode()`

**Choice:** Compute `config_x` from the *target final X mirror* and the current `display_orientation`, rather than hardcoding a per-mode bool:
```
config_x = is_portrait ? !target_final_x : target_final_x
```

| Mode | target_final_x | Portrait config_x | Landscape config_x |
|---|---|---|---|
| Normal | false | true | false |
| LCD_mirror | true | false | true |
| DLP_normal | false | true | false |

**Rationale:** The same dialog is used for any SLA printer. Hardcoding per-orientation values for a portrait-only assumption would silently break landscape printers added later.

**Alternative considered:** Store final mirror booleans directly and correct the Trafo inversion at rasterization time. Rejected — the Trafo inversion is an intentional coordinate system convention that compensates for the XY-flip side effect of portrait rotation; changing it would affect all portrait printers across all output formats.

---

### Decision 3 — Legacy fallback without DLP_normal reconstruction

**Choice:** When `display_mirror_mode` is absent, `mirror_mode_from_config()` computes `final_x` from `config_x` and `display_orientation`, then classifies: `final_x=true → LCD_mirror`, otherwise `Normal`. DLP_normal is never reconstructed from legacy booleans.

**Rationale:** System presets searched during this work contain no instance of the DLP_normal bool combination in active presets. User presets that do have it would see `Normal` displayed — an acceptable degradation, since both modes produce identical raster output. Inventing a fourth "unknown" UI state or forcing a migration for a hypothetical legacy case adds complexity with no confirmed user benefit.

---

### Decision 4 — JSON boolean format `"1"` / `"0"`

**Choice:** All `coBool` fields in the three machine preset JSONs use `"1"` (true) or `"0"` (false), not `"true"`/`"false"`.

**Rationale:** `ConfigOptionBool::deserialize()` accepts only `"1"` and `"0"`. Unrecognised strings fall through to the substitution path, which calls `enum_looks_like_true_value()` — this function returns `false` for both `"true"` and `"false"` (only `"enabled"` / `"on"` return `true`). The result is that `"true"` silently parses as `false`, corrupting LCD_mirror presets.

## Risks / Trade-offs

**[Risk] Portrait-only logic is not self-documenting** → Mitigation: The orientation-aware formula is extracted into a single place (`apply_mirror_mode()`); the complementary read path (`mirror_mode_from_config()`) uses the same formula; both are commented with a reference to `RasterBase::Trafo`.

**[Risk] User presets with legacy DLP_normal booleans display as Normal** → Mitigation: Both modes produce identical raster output, so the mismatch is UI-cosmetic only. If a user re-saves while Normal is displayed, the JSON will be rewritten with `display_mirror_mode=normal`, which is behaviourally identical.

**[Risk] Default value for `display_mirror_mode` is `lcd_mirror`** → Rationale: preserves the pre-existing implicit behaviour (most Phrozen system presets were already LCD printers with no explicit mirror key; the global default was `display_mirror_x=true`, which in landscape mode means LCD). New presets that use the dialog always get an explicit key written.
