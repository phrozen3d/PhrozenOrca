## Why

`sla-support-weight` 功能（commit `0ac67bee7`）已在 `filter()` 階段正確縮放每個手動支撐點的頭部半徑（Light=0.5x、Medium=1.0x、Heavy=2.0x），但後續的幾何運算（特別是柱間橋接的最小距離檢查）仍使用全域預設值 `head_back_radius_mm`。這導致兩個 Heavy 支撐柱（實際半徑 1.0mm）在 1.5mm 近距離時被誤判為安全，最終產生物理交錯的支撐結構。

## What Changes

- 修改 `interconnect()` 的最小柱間距離檢查：由 `2 * head_back_radius_mm`（固定全域值）改為 `pillar.r_start + nextpillar.r_start`（兩柱實際半徑之和），確保橋段能物理容納於兩柱間距內
- 修改 `interconnect_pillars()` 補強柱的起始搜尋半徑：改為 `max(2 * base_radius_mm, 2 * pillar.r_start)`，避免補強柱放置在與 Heavy 柱過近的位置
- 修改 `filter()` 的重複點去除邏輯：在 D_SP=0.1mm cluster 中保留 weight 最高的點，而非直接取 `a.front()`，避免因排序偶然丟棄使用者明確設置的 Heavy 點

## Capabilities

### New Capabilities

- `sla-support-weight-geometry`：支撐樹幾何運算（柱間橋接距離、補強柱搜尋、重複點去除）必須使用各柱子由 weight 縮放後的實際半徑，不得使用全域預設值

### Modified Capabilities

（無 spec-level 行為變更；`sla-support-weight` 的 spec 未在 `openspec/specs/` 中建立，本次新增 `sla-support-weight-geometry` 覆蓋此幾何面向）

## Impact

**修改的檔案：**
- `src/libslic3r/SLA/SupportTreeBuildsteps.cpp`
  - `interconnect()` 行 321：最小柱間距檢查
  - `interconnect_pillars()` 行 1241：補強柱起始搜尋半徑 `r`
  - `filter()` 行 657-659：cluster 保留點策略

**不需修改：**
- `SupportPoint.hpp`（SupportWeight enum 已完整定義）
- `create_ground_pillar()`（allow_widening 邏輯已正確）
- GUI / 序列化層（無行為變更）
- FDM 支撐程式碼
