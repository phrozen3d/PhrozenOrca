# Spec: sla-weight-preset

## MODIFIED Requirements

### Requirement: L/M/H 切換更新全域 print config 並同步更新 Gizmo 面板狀態
當使用者在支撐點 Gizmo 選取 Light、Medium 或 Heavy 時，系統 SHALL 呼叫 `cfg.set()` 更新全域 SLA print config，並觸發支撐頁面（Support tab）即時刷新；同時更新 Gizmo 內部狀態變數 `m_new_point_weight`、`m_new_point_head_diameter`、`m_new_point_pillar_diameter`。

被更新的支撐尺寸參數集合 SHALL 涵蓋 Support 分頁所顯示的六項尺寸：`support_contact_diameter`（接觸直徑）、`support_head_penetration`（接觸深度）、`support_head_front_diameter`（上端直徑）、`support_head_back_diameter`（較低直徑）、`support_segment_length`（段長度）、`support_pillar_diameter`（支撐柱直徑）。其中 `support_head_penetration` 與 `support_segment_length` MUST 隨檔位變動（不得於三檔間維持固定值）；`support_head_back_diameter` MUST 設為與該檔 `support_pillar_diameter` 相同之值。

#### Scenario: 切換 Heavy 後支撐頁面更新
- **WHEN** 使用者在 Gizmo 選取 Heavy
- **THEN** 支撐頁面的 `support_pillar_diameter` 等參數更新為 Heavy preset 值，Gizmo 面板同步顯示 Heavy 對應的頭部直徑與柱體直徑

#### Scenario: 切換檔位時接觸深度與段長度隨檔變動
- **WHEN** 使用者依序切換 Light、Middle、Heavy
- **THEN** `support_head_penetration` 分別更新為 0.3、0.4、0.6，`support_segment_length` 分別更新為 2、2、3（不維持固定值）

#### Scenario: 切換 Light 後下一個新增點使用 Light 尺寸
- **WHEN** 使用者在 Gizmo 選取 Light，再在模型上點擊新增支撐點
- **THEN** 新增的 `SupportPoint` 的 `weight` 為 `Light`，`pillar_radius` 為 Light preset 的 `pillar_diameter / 2`；支撐頁面已更新為 Light 全域設定

#### Scenario: 切換 Heavy 後選取既有 Light 點，Gizmo 面板顯示該點的尺寸，支撐頁面不變
- **WHEN** 使用者目前 Gizmo 狀態為 Heavy（支撐頁面已更新為 Heavy 值），點選一個先前以 Light 放置的支撐點
- **THEN** Gizmo 面板更新 `m_new_point_pillar_diameter` 為該點的 `pillar_radius * 2`，顯示 Light 的柱體直徑；支撐頁面維持 Heavy 值不變（不呼叫 cfg.set()）

## ADDED Requirements

### Requirement: L/M/H 三檔支撐尺寸預設值對齊 CHITUBOX
三檔 (Light / Middle / Heavy) 支撐尺寸預設值 SHALL 對齊 CHITUBOX，數值如下表（單位 mm）：

| 參數 | Light | Middle | Heavy |
|---|---|---|---|
| `support_contact_diameter`（接觸直徑）| 0.5 | 0.8 | 1.0 |
| `support_head_penetration`（接觸深度）| 0.3 | 0.4 | 0.6 |
| `support_head_front_diameter`（上端直徑）| 0.3 | 0.4 | 0.6 |
| `support_head_back_diameter`（較低直徑）| 0.8 | 1.2 | 1.5 |
| `support_segment_length`（段長度）| 2 | 2 | 3 |
| `support_pillar_diameter`（支撐柱直徑）| 0.8 | 1.2 | 1.5 |

`support_head_width` 不屬於本需求範圍，MUST NOT 被納入 CHITUBOX 對照調整。

#### Scenario: 套用 Light 檔的支撐尺寸
- **WHEN** 使用者在 Gizmo 選取 Light
- **THEN** 全域 config 的接觸直徑 0.5、接觸深度 0.3、上端直徑 0.3、較低直徑 0.8、段長度 2、支撐柱直徑 0.8

#### Scenario: 套用 Middle 檔的支撐尺寸
- **WHEN** 使用者在 Gizmo 選取 Middle
- **THEN** 全域 config 的接觸直徑 0.8、接觸深度 0.4、上端直徑 0.4、較低直徑 1.2、段長度 2、支撐柱直徑 1.2

#### Scenario: 套用 Heavy 檔的支撐尺寸
- **WHEN** 使用者在 Gizmo 選取 Heavy
- **THEN** 全域 config 的接觸直徑 1.0、接觸深度 0.6、上端直徑 0.6、較低直徑 1.5、段長度 3、支撐柱直徑 1.5

### Requirement: Phrozen SLA 系統預設載入時以 Middle 檔高亮且顯示值一致
Phrozen SLA 系統 process 預設的初始支撐尺寸 SHALL 與 Middle 檔一致，使開啟支撐點 Gizmo 時，`support_pillar_diameter` 反查比對命中 Middle、對應 radio 高亮，且 Support 分頁顯示值等同按下 Middle 之結果。系統預設的 `support_pillar_diameter` MUST 為 1.2、`support_segment_length` MUST 為 2、`support_head_back_diameter` MUST 為 1.2。

#### Scenario: 載入 Phrozen SLA 預設後開啟 Gizmo
- **WHEN** 使用者選用 Phrozen SLA 印表機/process 系統預設並開啟支撐點 Gizmo（未曾按任何檔位按鈕）
- **THEN** Middle radio 顯示為選取狀態，且 Support 分頁的支撐柱直徑為 1.2、段長度為 2、較低直徑為 1.2