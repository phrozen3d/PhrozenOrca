## ADDED Requirements

### Requirement: SupportPoint 儲存 per-point 重量分類
`SupportPoint` struct SHALL 新增 `SupportWeight weight` 列舉欄位，可取值為 `Light`、`Medium`、`Heavy`，預設值為 `Medium`。

#### Scenario: 新建支撐點的預設重量
- **WHEN** 使用者點擊放置新支撐點且未選擇重量時
- **THEN** `SupportPoint.weight` SHALL 為 `SupportWeight::Medium`

#### Scenario: 使用者選擇 Light 後放置點
- **WHEN** 使用者在 GLGizmo 面板選擇 Light 後點擊放置支撐點
- **THEN** `SupportPoint.weight` SHALL 為 `SupportWeight::Light`

#### Scenario: 使用者選擇 Heavy 後放置點
- **WHEN** 使用者在 GLGizmo 面板選擇 Heavy 後點擊放置支撐點
- **THEN** `SupportPoint.weight` SHALL 為 `SupportWeight::Heavy`

---

### Requirement: manual_add 點根據 weight 縮放柱子半徑
在支撐樹建構過程中，`type == manual_add` 的支撐點 SHALL 根據 `weight` 縮放 `back_r`。

#### Scenario: Light 點柱子半徑縮小
- **WHEN** 支撐點 `type == manual_add` 且 `weight == Light` 時建構支撐樹
- **THEN** 該點的 `back_r` SHALL 為全域 `head_back_radius_mm * 0.6`

#### Scenario: Medium 點柱子半徑不縮放
- **WHEN** 支撐點 `type == manual_add` 且 `weight == Medium` 時建構支撐樹
- **THEN** 該點的 `back_r` SHALL 為全域 `head_back_radius_mm * 1.0`

#### Scenario: Heavy 點柱子半徑放大
- **WHEN** 支撐點 `type == manual_add` 且 `weight == Heavy` 時建構支撐樹
- **THEN** 該點的 `back_r` SHALL 為全域 `head_back_radius_mm * 1.4`

#### Scenario: 自動生成點不受 weight 影響
- **WHEN** 支撐點 `type == island` 或 `type == slope` 時建構支撐樹
- **THEN** 該點的 `back_r` SHALL 為全域 `head_back_radius_mm`（不縮放，忽略 weight 欄位）

---

### Requirement: GLGizmo 面板提供重量選擇器
SLA 支撐 gizmo 的手動模式面板 SHALL 顯示 Light / Medium / Heavy 選擇器，控制後續放置點的重量。

#### Scenario: 選擇器預設為 Medium
- **WHEN** 進入 SLA 支撐 gizmo 編輯模式時
- **THEN** 重量選擇器 SHALL 預設顯示 Medium

#### Scenario: 切換選擇器後新點使用新重量
- **WHEN** 使用者將選擇器切換至 Heavy，然後點擊放置支撐點
- **THEN** 新放置的點 SHALL 具有 `weight == Heavy`

#### Scenario: 切換選擇器不影響已存在的點
- **WHEN** 使用者切換重量選擇器時
- **THEN** 已存在的支撐點 weight SHALL 不改變

---

### Requirement: 3mf 序列化向後相容
`SupportWeight` SHALL 序列化為第六個浮點數（`Light=0.0`、`Medium=1.0`、`Heavy=2.0`），讀取舊格式時若欄位缺失 SHALL 預設為 `Medium`。

#### Scenario: 新檔案包含 weight 欄位
- **WHEN** 含有 SupportWeight 的模型儲存至 3mf 時
- **THEN** 每個支撐點 SHALL 有六個浮點數，第六個為 weight 的編碼值

#### Scenario: 舊檔案載入預設 Medium
- **WHEN** 載入只有五個浮點數的舊格式 3mf 時
- **THEN** 所有點的 weight SHALL 預設為 `SupportWeight::Medium`

#### Scenario: 儲存並重新載入的來回測試
- **WHEN** 含有 Light/Medium/Heavy 混合點的模型儲存並重新載入時
- **THEN** 所有點的 weight SHALL 完整保留
