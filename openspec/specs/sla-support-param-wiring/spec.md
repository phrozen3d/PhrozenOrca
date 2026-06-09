# Spec: SLA Support Param Wiring

## Purpose

確保 Resin 支撐設定頁的 SLA 參數正確登記於 `SLAPrintObjectConfig`、透過 `make_support_cfg()` 傳入支撐引擎，並在變更時觸發支撐樹重算。
## Requirements
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

### Requirement: 支撐角度參數傳入演算法
系統 SHALL 確保 `angle_between_top_and_middle` 與 `cross_angle` 透過 `SLAPrintObjectConfig` 傳入 `make_support_cfg()`，分別映射至 `SupportTreeConfig::top_middle_slope` 與 `SupportTreeConfig::cross_slope`，並最終影響 Default Tree 支撐幾何生成。

#### Scenario: angle_between_top_and_middle 傳入引擎
- **WHEN** `angle_between_top_and_middle` 設為 30° 並執行 Default Tree 切片
- **THEN** `SupportTreeConfig::top_middle_slope` SHALL 為 30° 的弧度值

#### Scenario: cross_angle 傳入引擎
- **WHEN** `cross_angle` 設為 60° 並執行 Default Tree 切片
- **THEN** `SupportTreeConfig::cross_slope` SHALL 為 60° 的弧度值

### Requirement: 支撐角度參數異動觸發重切
系統 SHALL 在 `angle_between_top_and_middle` 或 `cross_angle` 任一值變更時，自動將支撐樹狀態標記為 dirty 並觸發重切。`support_bracing_angle` 變更時的行為 SHALL 維持不變（仍觸發重切）。

#### Scenario: 修改 angle_between_top_and_middle 後狀態失效
- **WHEN** 使用者在設定頁更改 `angle_between_top_and_middle`
- **THEN** `SLAPrintObject` 的 `slaposSupportTree` 步驟狀態 SHALL 標記為需重新計算

#### Scenario: 修改 cross_angle 後狀態失效
- **WHEN** 使用者在設定頁更改 `cross_angle`
- **THEN** `SLAPrintObject` 的 `slaposSupportTree` 步驟狀態 SHALL 標記為需重新計算

### Requirement: support_object_elevation 傳入支撐引擎

系統 SHALL 確保 `support_object_elevation`（UI 標籤：Model Lift Height）透過 `SLAPrintObjectConfig` 傳入 `make_support_cfg()`，映射至 `SupportTreeConfig::object_elevation_mm`，並影響支撐樹生成時模型與平台/raft 的垂直間距。當 `is_zero_elevation()` 為 true 時，`object_elevation_mm` SHALL 為 0。

#### Scenario: Model Lift Height 傳入引擎

- **WHEN** `support_object_elevation` 設為 6 mm 且非 zero-elevation 模式，並執行支撐樹切片
- **THEN** `SupportTreeConfig::object_elevation_mm` SHALL 為 6（或經 pinhead 最小值 clamp 後的不小於該下限的值）

#### Scenario: 參數異動觸發重切

- **WHEN** 使用者更改 `support_object_elevation`
- **THEN** `SLAPrintObject` 的 `slaposObjectSlice` 及後續支撐相關步驟 SHALL 標記為需重新計算

