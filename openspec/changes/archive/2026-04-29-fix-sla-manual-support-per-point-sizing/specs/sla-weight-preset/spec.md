## MODIFIED Requirements

### Requirement: L/M/H 切換更新全域 print config 並同步更新 Gizmo 面板狀態
當使用者在支撐點 Gizmo 選取 Light、Medium 或 Heavy 時，系統 SHALL 呼叫 `cfg.set()` 更新全域 SLA print config（6 個支撐參數），並觸發支撐頁面（Support tab）即時刷新；同時更新 Gizmo 內部狀態變數 `m_new_point_weight`、`m_new_point_head_diameter`、`m_new_point_pillar_diameter`。

#### Scenario: 切換 Heavy 後支撐頁面更新
- **WHEN** 使用者在 Gizmo 選取 Heavy
- **THEN** 支撐頁面的 `support_pillar_diameter` 等參數更新為 Heavy preset 值，Gizmo 面板同步顯示 Heavy 對應的頭部直徑與柱體直徑

#### Scenario: 切換 Light 後下一個新增點使用 Light 尺寸
- **WHEN** 使用者在 Gizmo 選取 Light，再在模型上點擊新增支撐點
- **THEN** 新增的 `SupportPoint` 的 `weight` 為 `Light`，`pillar_radius` 為 Light preset 的 `pillar_diameter / 2`；支撐頁面已更新為 Light 全域設定

#### Scenario: 切換 Heavy 後選取既有 Light 點，Gizmo 面板顯示該點的尺寸，支撐頁面不變
- **WHEN** 使用者目前 Gizmo 狀態為 Heavy（支撐頁面已更新為 Heavy 值），點選一個先前以 Light 放置的支撐點
- **THEN** Gizmo 面板更新 `m_new_point_pillar_diameter` 為該點的 `pillar_radius * 2`，顯示 Light 的柱體直徑；支撐頁面維持 Heavy 值不變（不呼叫 cfg.set()）
