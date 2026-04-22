## ADDED Requirements

### Requirement: L/M/H 切換寫入全域 print config 並觸發重切
當使用者在支撐點 Gizmo 選取 Light、Medium 或 Heavy 時，系統 SHALL 將對應的 preset 數值（柱徑、前球直徑、接觸球直徑、底座尺寸、底座高度、頭部寬度）寫入全域 SLA print config，並觸發 Support 設定頁更新及支撐樹重切。

#### Scenario: 切換 Heavy 後參數更新
- **WHEN** 使用者在 Gizmo 選取 Heavy
- **THEN** `support_pillar_diameter`、`support_head_front_diameter` 等欄位須更新為 Heavy preset 數值，Support 設定頁顯示的數值隨之更新，並自動觸發重切

#### Scenario: 切換 Light 後參數更新
- **WHEN** 使用者在 Gizmo 選取 Light
- **THEN** 對應欄位更新為 Light preset 數值，自動觸發重切

### Requirement: Gizmo 初始化時同步 radio 狀態
開啟支撐點 Gizmo 時，系統 SHALL 讀取目前 print config 的 `support_pillar_diameter`，與三組 preset 的柱徑做精確比對，並將匹配的 radio button 設為選取狀態；若無匹配則不選取任何 radio（`weight_int = -1`）。

#### Scenario: 目前柱徑與 Medium preset 吻合
- **WHEN** 開啟 Gizmo，且目前 `support_pillar_diameter` 等於 Medium preset 的柱徑值
- **THEN** Medium radio button 顯示為選取狀態

#### Scenario: 目前柱徑無法匹配任一 preset
- **WHEN** 開啟 Gizmo，且目前 `support_pillar_diameter` 與三組 preset 均不吻合
- **THEN** 三個 radio button 均不選取（`weight_int = -1`）

### Requirement: 演算法不以 SupportPoint::weight 做倍率計算或叢集排序
支撐樹演算法 SHALL NOT 讀取 `SupportPoint::weight` 做倍率計算或叢集排序。

**手動放置點**：演算法 SHALL 讀取每點的 `weight`，從 `SupportTreeConfig` 中對應的 `light_back_radius_mm`、`medium_back_radius_mm`、`heavy_back_radius_mm` 選取後球半徑，使每個手動點的柱徑忠實反映放置時的 L/M/H 選擇。`widen`（柱徑加寬）邏輯對 Heavy 手動點套用，Light / Medium 手動點則跳過（避免細支撐被強制加粗）。

**自動產生點**：不讀取 `weight`，統一使用 `m_cfg.head_back_radius_mm`（由全域 print config 決定）。

#### Scenario: 手動 Heavy 點柱徑反映 Heavy preset
- **WHEN** 使用者放置一個 Heavy 支撐點
- **THEN** 支撐柱使用 `heavy_back_radius_mm`（Heavy preset 的柱徑），無論全域 config 當下值為何

#### Scenario: 手動 Light 點柱徑反映 Light preset
- **WHEN** 使用者放置一個 Light 支撐點
- **THEN** 支撐柱使用 `light_back_radius_mm`，且不套用柱徑加寬邏輯

#### Scenario: 叢集中保留第一個點
- **WHEN** 多個支撐點落入同一叢集，其中各點 `weight` 不同
- **THEN** 叢集保留第一個點（迭代順序），不依 `weight` 排序
