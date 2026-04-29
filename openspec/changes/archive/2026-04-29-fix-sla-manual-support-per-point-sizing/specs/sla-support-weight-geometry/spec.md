## MODIFIED Requirements

### Requirement: L/M/H 切換更新全域 print config，並在放點時記錄 pillar_radius
當使用者在支撐點 Gizmo 選取 Light、Medium 或 Heavy 時，系統 SHALL 呼叫 `cfg.set()` 更新全域 SLA print config 並刷新支撐頁面。放置手動支撐點時，系統 SHALL 將當下的 `m_new_point_pillar_diameter / 2` 存入 `SupportPoint::pillar_radius`。

#### Scenario: 切換 Heavy 後支撐頁面更新
- **WHEN** 使用者在 Gizmo 選取 Heavy
- **THEN** 全域 `support_pillar_diameter` 更新為 Heavy preset 值，Support 設定頁刷新

#### Scenario: 切換後新增的點帶有正確 pillar_radius
- **WHEN** 使用者選取 Light 後在模型上新增支撐點
- **THEN** 新點的 `SupportPoint::pillar_radius` 等於 Light preset 的 `pillar_diameter / 2`；`SupportPoint::weight` 為 `Light`

### Requirement: 演算法使用 SupportPoint::pillar_radius 決定柱徑，不以 weight 做倍率計算或叢集排序
支撐樹演算法 SHALL NOT 讀取 `SupportPoint::weight` 做倍率計算或叢集排序。

**手動放置點**：演算法 SHALL 讀取每點的 `pillar_radius`。若 `pillar_radius > 0.f`，以其作為 `back_r` 傳入 `filterfn`，使每個手動點的柱徑忠實反映放置時的設定值。`widen`（柱徑加寬）行為由 `filterfn` 內部的 `back_r < head_back_radius_mm` 比較自動決定：Light 點的 pillar_radius 較小，head width 自然縮短，不需額外條件。

**自動產生點**：`pillar_radius == 0.f`，統一使用 `m_cfg.head_back_radius_mm`（由全域 print config 決定）。

#### Scenario: 手動點柱徑反映放置時的 pillar_radius
- **WHEN** 使用者放置一個 Heavy 手動支撐點（pillar_radius = 0.75mm），全域 `head_back_radius_mm` 為 0.5mm
- **THEN** 支撐柱使用 `back_r = 0.75mm`，不受全域值影響

#### Scenario: 自動生成點仍使用全域 head_back_radius_mm
- **WHEN** 自動支撐演算法生成一個 `SupportPointType::island` 點（pillar_radius = 0.f）
- **THEN** 對應支撐柱使用全域 `m_cfg.head_back_radius_mm`，不讀取 pillar_radius

#### Scenario: 叢集中保留第一個點
- **WHEN** 多個支撐點落入同一叢集，其中各點 `pillar_radius` 不同
- **THEN** 叢集保留第一個點（迭代順序），不依 `pillar_radius` 或 `weight` 排序

## ADDED Requirements

### Requirement: 3MF 格式 version 2 持久化 SupportPoint pillar_radius
`Slic3r_PE_sla_support_points.txt` 格式 SHALL 升級至 version 2，每個支撐點存 6 個 float（x, y, z, head_front_radius, type_f, pillar_radius_f）。讀取 version 1 檔案時 `pillar_radius` SHALL 設為 `0.f`（使用全域設定，切片行為等同修改前）。pillar_radius_f 以實際 mm 值存入。

#### Scenario: version 2 存檔後重開 pillar_radius 保留
- **WHEN** 使用者新增 Light 手動支撐點（pillar_radius = 0.3mm），存檔為 .3mf，重新開啟
- **THEN** 該點的 `pillar_radius` 仍為 0.3mm，切片後柱體使用此值

#### Scenario: version 1 舊檔重開 pillar_radius 為 0.f
- **WHEN** 開啟一個以 version 1 格式存入的 .3mf 檔案
- **THEN** 所有支撐點的 `pillar_radius` 為 `0.f`，切片使用全域 `head_back_radius_mm`，行為與修改前一致
