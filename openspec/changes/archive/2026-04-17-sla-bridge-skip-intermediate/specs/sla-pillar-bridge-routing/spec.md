## ADDED Requirements

### Requirement: 支撐柱橋接按最近距離優先
在 `cascadefn` 中，同一根柱子的橋接候選鄰居 SHALL 依距離由近到遠排序，系統 SHALL 優先嘗試最近的鄰居；距離排序 SHALL 於過濾任何條件之前完成。

#### Scenario: 距離最近的鄰居優先被嘗試
- **WHEN** 一根柱子有多個在搜尋半徑內的鄰居
- **THEN** 系統 SHALL 按距離升序嘗試各鄰居，最近者最先被嘗試橋接

#### Scenario: 最近鄰居失敗後繼續嘗試次近者
- **WHEN** 最近鄰居因物理 min-dist 不足或已達連接上限而無法橋接
- **THEN** 系統 SHALL 繼續嘗試下一個最近的合法鄰居，不提前中止

### Requirement: 不依 r_start 大小跳過鄰居
`cascadefn` 在遍歷橋接候選鄰居時，SHALL NOT 因鄰居的 `r_start` 小於自身 `r_start` 而略過該鄰居。任何 weight 組合（Heavy→Medium、Heavy→Light、Medium→Light）皆 SHALL 被允許嘗試橋接，物理合法性 SHALL 由 `interconnect()` 的 min-dist 邏輯決定。

#### Scenario: Heavy 柱子與最近的 Medium 鄰居橋接
- **WHEN** 一根 Heavy 柱子（r_start=1.0mm）的最近鄰居是一根 Medium 柱子（r_start=0.5mm）
- **THEN** 系統 SHALL 嘗試橋接 Heavy 與該 Medium 柱子，不因半徑差異跳過

#### Scenario: Heavy 柱子不跳越 Medium 連到遠端 Heavy
- **WHEN** 一根 Heavy 柱子的搜尋範圍內有近距離的 Medium 柱子與遠距離的另一根 Heavy 柱子
- **THEN** 系統 SHALL 先嘗試近距離的 Medium 柱子；若 Medium 橋接成功，SHALL 不再試圖連接遠端 Heavy

#### Scenario: Medium 柱子與最近的 Light 鄰居橋接
- **WHEN** 一根 Medium 柱子（r_start=0.5mm）的最近鄰居是一根 Light 柱子（r_start=0.25mm）
- **THEN** 系統 SHALL 嘗試橋接 Medium 與該 Light 柱子，不因半徑差異跳過

#### Scenario: 相同 weight 的柱子仍可正常橋接
- **WHEN** 兩根相同 weight 的柱子（如兩根 Heavy）在搜尋半徑內且物理上可橋接
- **THEN** 系統 SHALL 正常橋接，行為與修改前一致

### Requirement: 橋接物理合法性由 interconnect() 負責
`cascadefn` 中判斷兩根柱子可否橋接的物理條件 SHALL 由 `interconnect()` 內部的 min-dist 檢查（`pillar_dist >= pillar.r_start + nextpillar.r_start`）負責，`cascadefn` 本身 SHALL NOT 重複或代替此驗證。

#### Scenario: 物理上無法容納橋段的鄰居被略過
- **WHEN** 兩根柱子因距離小於兩柱半徑之和而無法容納橋段
- **THEN** `interconnect()` SHALL 返回 false，`cascadefn` SHALL 繼續嘗試下一個鄰居，不建立橋段

#### Scenario: 物理上合法的跨 weight 橋段被建立
- **WHEN** Heavy 柱子（r=1.0mm）與 Medium 柱子（r=0.5mm）相距 2.0mm（≥ 1.0+0.5=1.5mm）
- **THEN** 系統 SHALL 建立橋段，橋段半徑 SHALL 為 min(1.0, 0.5) = 0.5mm
