## ADDED Requirements

### Requirement: SLA 支撐樹最佳化器使用高精度收斂條件
`SupportTreeConfig` 中的最佳化器精度常數 SHALL 與 PrusaSlicer 對齊：`optimizer_rel_score_diff = 1e-10`，`optimizer_max_iterations = 2000`。

#### Scenario: rel_score_diff 常數值
- **WHEN** 讀取 `SupportTreeConfig::optimizer_rel_score_diff`
- **THEN** 其值 SHALL 為 `1e-10`

#### Scenario: max_iterations 常數值
- **WHEN** 讀取 `SupportTreeConfig::optimizer_max_iterations`
- **THEN** 其值 SHALL 為 `2000`

#### Scenario: 最佳化器使用此常數
- **WHEN** `SupportTreeBuildsteps` 建立 NLopt 最佳化器時
- **THEN** SHALL 以 `cfg.optimizer_rel_score_diff` 設定 `.rel_score_diff()`，以 `cfg.optimizer_max_iterations` 設定 `.max_iterations()`
