## Why

當兩根不同半徑的 SLA 支撐柱相互連接時，目前連接橋（bridge/crossbridge）的直徑固定沿用其中一根柱的半徑，導致橋的直徑可能大於較細的那根柱，造成幾何上不合理的「細柱接粗橋」現象，影響列印品質與視覺呈現。

## What Changes

- `interconnect()` 中建立水平 crossbridge 時，改用兩柱半徑的最小值（`min(pillar.r_start, nextpillar.r_start)`）
- `connect_to_nearpillar()` 中建立斜向 bridge（head 連至旁邊柱）時，改用 head 半徑與目標柱半徑的最小值（`min(head.r_back_mm, nearpillar.r_start)`）
- 碰撞偵測（`bridge_mesh_distance`）同步使用更新後的橋接半徑，確保空間檢查一致

## Capabilities

### New Capabilities
- `sla-bridge-diameter-min-pillar`: 連接兩根支撐柱的 bridge（水平 crossbridge 與斜向 bridge）其直徑等於兩端中較細者的直徑

### Modified Capabilities

（無既有 spec 需修改）

## Impact

- **修改檔案**：`src/libslic3r/SLA/SupportTreeBuildsteps.cpp`
  - `interconnect()`：行 359、361、371、373
  - `connect_to_nearpillar()`：行 399
- **不影響**：`DiffBridge` 結構體、branching tree 策略的 `build_ground_connection()`、手動支撐點獨立柱邏輯
- **無 API / 設定檔層面的 breaking change**
