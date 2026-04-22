## ADDED Requirements

### Requirement: Contact Sphere 模式幾何
當 `support_contact_type` 設為 `Sphere` 且 `support_contact_diameter` 大於 pinhead 前球直徑時，系統 SHALL 在 pinhead 前球的相同圓心位置疊加一個半徑為 `support_contact_diameter / 2` 的完整球體，以增加支撐與模型表面的接觸面積。

#### Scenario: Sphere 模式產生接觸球
- **WHEN** `support_contact_type = Sphere`、`support_contact_diameter > support_head_front_diameter`，執行切片
- **THEN** 每個 pinhead mesh 須包含一個與前球同心、半徑更大的接觸球幾何

#### Scenario: 接觸球直徑不大於前球時不產生球
- **WHEN** `support_contact_type = Sphere`，但 `support_contact_diameter <= support_head_front_diameter`
- **THEN** 系統不附加接觸球，輸出與標準 pinhead 相同

#### Scenario: None 模式不產生接觸球
- **WHEN** `support_contact_type = None`（預設）
- **THEN** pinhead mesh 與標準幾何一致，無額外球體

### Requirement: Contact Sphere 參數異動觸發重切
系統 SHALL 在 `support_contact_type` 或 `support_contact_diameter` 變更時，自動將 `slaposSupportTree` 步驟標記為需重切。

#### Scenario: 切換 contact_type 觸發重切
- **WHEN** 使用者將 `support_contact_type` 從 `None` 切換為 `Sphere`（或反向）
- **THEN** 支撐樹須重新生成

#### Scenario: 修改接觸球直徑觸發重切
- **WHEN** 使用者修改 `support_contact_diameter`
- **THEN** 支撐樹須重新生成
