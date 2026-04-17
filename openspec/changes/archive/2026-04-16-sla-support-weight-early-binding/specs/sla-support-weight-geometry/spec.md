## ADDED Requirements

### Requirement: Pillar interconnect minimum distance uses actual pillar radii
支撐樹在連接兩根相鄰柱子時，SHALL 以「兩柱實際半徑之和」作為最小允許中心間距，不得使用全域預設值 `head_back_radius_mm`。當兩柱中心距離小於 `pillar.r_start + nextpillar.r_start` 時，系統 SHALL 跳過此對柱子的橋接嘗試。

#### Scenario: 兩根 Heavy 柱距離不足時不建橋
- **WHEN** 兩根 Heavy 手動支撐柱（r_start = 1.0mm 各）中心間距為 1.5mm
- **THEN** `interconnect()` 不為此對柱子建立橋段，回傳 false

#### Scenario: 兩根 Heavy 柱距離充足時可建橋
- **WHEN** 兩根 Heavy 手動支撐柱（r_start = 1.0mm 各）中心間距為 2.5mm
- **THEN** `interconnect()` 可嘗試為此對柱子建立橋段（視碰撞偵測結果決定是否實際建橋）

#### Scenario: Medium 柱行為與修改前一致
- **WHEN** 兩根 Medium 手動支撐柱（r_start = 0.5mm 各）中心間距為 0.8mm（> 2 * 0.5mm）
- **THEN** `interconnect()` 行為與修改前相同（允許嘗試橋接）

#### Scenario: Light 與 Heavy 混搭
- **WHEN** 一根 Light 柱（r_start = 0.25mm）與一根 Heavy 柱（r_start = 1.0mm）中心間距為 1.1mm
- **THEN** 最小間距為 0.25 + 1.0 = 1.25mm，1.1 < 1.25，系統 SHALL 跳過橋接

### Requirement: 補強柱搜尋半徑不小於目標柱實際半徑的兩倍
當 `interconnect_pillars()` 需要在某柱旁放置補強柱時，起始搜尋圓半徑 SHALL 不小於 `2 * pillar.r_start`，以確保補強柱落地點不與目標柱物理重疊。

#### Scenario: Heavy 柱的補強柱搜尋半徑
- **WHEN** 一根 Heavy 柱（r_start = 1.0mm）需要補強柱
- **THEN** 起始搜尋圓半徑 SHALL 至少為 2.0mm

#### Scenario: Light 柱的搜尋半徑不縮小於 base_radius_mm 的兩倍
- **WHEN** 一根 Light 柱（r_start = 0.25mm）需要補強柱，且 `base_radius_mm = 2.0mm`
- **THEN** 起始搜尋圓半徑 SHALL 至少為 `2 * base_radius_mm = 4.0mm`（取 max，不因 Light 而縮小）

### Requirement: 重複點去除時優先保留 weight 較高的支撐點
在 `filter()` 的去重 cluster 處理（D_SP=0.1mm）中，當一個 cluster 包含多個支撐點時，系統 SHALL 保留 `SupportWeight` 值最高的那個點（Heavy > Medium > Light）。若 weight 並列，則保留排序最前的點。

#### Scenario: cluster 中有 Light 和 Heavy 點
- **WHEN** 兩個手動支撐點距離 < 0.1mm，其中一個為 Light，另一個為 Heavy
- **THEN** 系統 SHALL 保留 Heavy 點，丟棄 Light 點

#### Scenario: cluster 中所有點 weight 相同
- **WHEN** 兩個 Medium 手動支撐點距離 < 0.1mm
- **THEN** 系統 SHALL 保留其中一個（行為與修改前相同）
