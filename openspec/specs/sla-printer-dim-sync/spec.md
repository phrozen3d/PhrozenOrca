# Spec: sla-printer-dim-sync

## Purpose

Ensure that the three SLA printer platform dimension config keys (`printable_area`, `display_width`, `display_height`) remain consistent whenever the user modifies or resets Size X/Y in `SLAPrinterSettingsDialog`. This prevents silent desync between the UI-visible polygon and the scalar values consumed by rasterization and PRZ export.

## Requirements

### Requirement: sync_local_to_tab() 必須同步寫回三個平台尺寸 config key

`SLAPrinterSettingsDialog::sync_local_to_tab()` 在將 Size X/Y 寫入 `printable_area` 的同時，SHALL 同步呼叫 `set_key_value` 寫入 `display_width`（等於 size_x）與 `display_height`（等於 size_y），確保三個 config key 始終保持一致：

- `printable_area`：四角頂點多邊形，bounding box 的 width = size_x，height = size_y
- `display_width`：`ConfigOptionFloat`，值 = size_x（mm）
- `display_height`：`ConfigOptionFloat`，值 = size_y（mm）

#### Scenario: 使用者修改 Size X/Y 後三個 key 一致

- **WHEN** 使用者在 `SLAPrinterSettingsDialog` 修改 Size X 與 Size Y 並觸發 `sync_local_to_tab()`
- **THEN** `display_width` 的值等於使用者輸入的 Size X（mm），`display_height` 的值等於 Size Y（mm），且 `printable_area` 的 bounding box 與兩者相符

#### Scenario: PRZ 匯出反映使用者修改的平台尺寸

- **WHEN** 使用者在對話框修改平台尺寸後匯出 PRZ 檔案
- **THEN** PRZ header 中的 `PlatformXLength`（= `display_height` after portrait swap）與 `PlatformYLength`（= `display_width` after portrait swap）反映使用者的設定，而非 JSON profile 的原始值

#### Scenario: Rasterization 中心點計算使用修改後的尺寸

- **WHEN** 使用者修改平台尺寸後執行切片（slapsRasterize 步驟）
- **THEN** `SLAPrintSteps.cpp` 中讀取 `display_width`/`display_height` 的 raster shift 計算結果與使用者設定的 `printable_area` 中心點一致

---

### Requirement: Reset 路徑執行後三個 key 保持一致

當使用者點擊 Size X/Y 欄位的 Reset 按鈕（`reset_field_to_default`），最終 SHALL 呼叫 `sync_local_to_tab(true)`，其執行結果必須使三個 key 一致還原至預設值。

#### Scenario: Reset 後 display_width/display_height 還原為 profile 預設值

- **WHEN** 使用者點擊 Size X 或 Size Y 欄位的 Reset 按鈕
- **THEN** `sync_local_to_tab(true)` 被呼叫，且 `display_width`、`display_height`、`printable_area` 均還原為對應 JSON profile 的預設值

---

### Requirement: 修復範圍不涉及 portrait swap 邏輯

`prz_header()` 中 `PlatformXLength` ← `display_height`、`PlatformYLength` ← `display_width` 的交換邏輯，以及 `XResolution` ← `display_pixels_y`、`YResolution` ← `display_pixels_x` 的像素交換，SHALL 保持不動。

#### Scenario: portrait swap 邏輯不受修復影響

- **WHEN** `prz_header()` 在修復後被呼叫
- **THEN** `PlatformXLength` 仍讀取 `display_height`，`PlatformYLength` 仍讀取 `display_width`，行為與修復前完全相同（僅值變為正確）
