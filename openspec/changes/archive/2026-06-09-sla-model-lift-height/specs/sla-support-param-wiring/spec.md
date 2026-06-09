## ADDED Requirements

### Requirement: support_object_elevation 傳入支撐引擎

系統 SHALL 確保 `support_object_elevation`（UI 標籤：Model Lift Height）透過 `SLAPrintObjectConfig` 傳入 `make_support_cfg()`，映射至 `SupportTreeConfig::object_elevation_mm`，並影響支撐樹生成時模型與平台/raft 的垂直間距。當 `is_zero_elevation()` 為 true 時，`object_elevation_mm` SHALL 為 0。

#### Scenario: Model Lift Height 傳入引擎

- **WHEN** `support_object_elevation` 設為 6 mm 且非 zero-elevation 模式，並執行支撐樹切片
- **THEN** `SupportTreeConfig::object_elevation_mm` SHALL 為 6（或經 pinhead 最小值 clamp 後的不小於該下限的值）

#### Scenario: 參數異動觸發重切

- **WHEN** 使用者更改 `support_object_elevation`
- **THEN** `SLAPrintObject` 的 `slaposObjectSlice` 及後續支撐相關步驟 SHALL 標記為需重新計算
