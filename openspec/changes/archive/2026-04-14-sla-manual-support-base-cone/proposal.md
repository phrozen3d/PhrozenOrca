## Why

當使用者在 Manual editing 模式手動放置 SLA 支撐點時，若兩個支撐點的 XY 水平距離小於 `2 × base_radius_mm`（預設 4mm），支撐樹生成系統會將它們歸入同一個 cluster，僅讓 cluster 中心點（centroid）建立完整的地面柱與底座圓錐，其他支撐點則橋接到 centroid 柱而沒有自己的底座，導致使用者觀察到「部分手動支撐沒有底座」的現象，與預期不符。

## What Changes

- 修改 `classify()` 的 clustering predicate：當兩個支撐頭（Head）中至少有一個來自手動放置的支撐點（`SupportPointType::manual_add`）時，不將它們歸入同一個 cluster；每個手動點均通過 `routing_to_ground()` 建立自己的地面柱與底座圓錐
- 修改 `create_ground_pillar()` 的 `can_add_base` 判斷：若對應的支撐點為 `manual_add`，一律強制 `can_add_base = true`，確保 Light 重量手動點（柱半徑 < `head_back_radius_mm`）也能生成底座圓錐
- 自動產生的支撐點的 clustering 行為與底座判斷邏輯維持不變

## Capabilities

### New Capabilities

- `sla-manual-support-base-cone`: 確保每個手動放置的 SLA 支撐點都能生成各自的底座圓錐（Pedestal），即使多個手動支撐點的水平距離小於底座直徑

### Modified Capabilities

<!-- 無既有 spec 需要修改 -->

## Impact

- **修改檔案**：`src/libslic3r/SLA/SupportTreeBuildsteps.cpp`，兩處：
  - `classify()` 函式內的 clustering predicate lambda（約第 824 行）
  - `create_ground_pillar()` 函式內第一次 `eval_limits()` 呼叫之後（約第 495 行）
- **不影響**：auto-generated 支撐點的行為、支撐柱橋接邏輯、Pad 生成、filterfn 角度過濾
- **幾何安全性**：重疊的底座圓錐透過 `its_merge()` 拼接，SLA 切層時自然合併為聯集輪廓，不影響列印品質