## ADDED Requirements

### Requirement: SLA 支撐 UI 參數傳入演算法
系統 SHALL 確保 Support 設定頁的 SLA 支撐參數（柱徑、底座尺寸、前球直徑、頭部寬度、穿透深度）透過 `SLAPrintObjectConfig` 傳入 `make_support_cfg()`，並最終影響支撐幾何生成。這些參數必須登記在 `SLAPrintObjectConfig` 中（而非 `PrintConfig`），使 `SLAPrint::apply()` 的 `diff()` 能正確取得它們。

#### Scenario: 修改柱徑後重切
- **WHEN** 使用者在 Support 設定頁修改 `support_pillar_diameter` 並執行切片
- **THEN** 生成的支撐柱直徑須與設定值一致

#### Scenario: 修改底座高度後重切
- **WHEN** 使用者修改 `support_base_height` 並執行切片
- **THEN** 支撐底座高度須反映新設定值

#### Scenario: 修改前球直徑後重切
- **WHEN** 使用者修改 `support_head_front_diameter` 並執行切片
- **THEN** 每個 pinhead 前球半徑須對應新設定值

### Requirement: 支撐參數異動觸發重切
系統 SHALL 在 `support_pillar_diameter`、`support_head_front_diameter`、`support_base_diameter`、`support_base_height`、`support_head_width`、`support_head_penetration` 任一值變更時，自動將支撐樹狀態標記為 dirty 並觸發重切。

#### Scenario: UI 參數異動後狀態失效
- **WHEN** 使用者在設定頁更改任一支撐幾何參數
- **THEN** `SLAPrintObject` 的 `slaposSupportTree` 步驟狀態須標記為需重新計算

#### Scenario: 未更改參數時不重切
- **WHEN** 使用者開啟設定頁但未修改任何支撐參數即關閉
- **THEN** 支撐樹狀態維持不變，不觸發不必要的重切
