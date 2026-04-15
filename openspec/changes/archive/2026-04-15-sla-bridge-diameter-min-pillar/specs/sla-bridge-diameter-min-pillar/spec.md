## ADDED Requirements

### Requirement: 水平 crossbridge 直徑不超過較細支撐柱
當 `interconnect()` 在兩根半徑不同的支撐柱之間建立水平 crossbridge 時，系統 SHALL 使用兩柱頂端半徑（`r_start`）的最小值作為橋的半徑，而非固定使用第一根柱的半徑。

#### Scenario: 兩根相同半徑的柱相互連接
- **WHEN** `pillar.r_start == nextpillar.r_start`
- **THEN** crossbridge 的半徑等於 `pillar.r_start`（行為與修改前相同）

#### Scenario: 粗柱連接細柱
- **WHEN** `pillar.r_start > nextpillar.r_start`
- **THEN** crossbridge 的半徑等於 `nextpillar.r_start`（使用較細的那根）

#### Scenario: 細柱連接粗柱
- **WHEN** `pillar.r_start < nextpillar.r_start`
- **THEN** crossbridge 的半徑等於 `pillar.r_start`（使用較細的那根）

#### Scenario: 碰撞偵測使用相同半徑
- **WHEN** 建立 crossbridge 前執行 `bridge_mesh_distance` 碰撞偵測
- **THEN** 偵測使用的半徑與最終建立的 crossbridge 半徑一致

### Requirement: 斜向 bridge 直徑不超過較細端
當 `connect_to_nearpillar()` 將一個 Head 連接至旁邊的支撐柱時，系統 SHALL 使用 `head.r_back_mm` 與目標柱頂端半徑（`nearpillar.r_start`）的最小值作為橋的半徑。

#### Scenario: Head 半徑小於目標柱半徑
- **WHEN** `head.r_back_mm < nearpillar.r_start`
- **THEN** 斜向 bridge 的半徑等於 `head.r_back_mm`

#### Scenario: Head 半徑大於目標柱半徑
- **WHEN** `head.r_back_mm > nearpillar.r_start`
- **THEN** 斜向 bridge 的半徑等於 `nearpillar.r_start`（使用較細的目標柱）

#### Scenario: 橋長限制計算一致
- **WHEN** 計算 `max_len`（最大橋長）以及後續碰撞偵測
- **THEN** 所有計算使用相同的修正後半徑，確保幾何一致性
