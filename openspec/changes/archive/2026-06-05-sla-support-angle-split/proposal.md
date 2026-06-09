## Why

`support_bracing_angle` 目前以單一 `bridge_slope` 同時控制兩種語意不同的支撐連接：接觸頭到主柱（Top↔Middle）與主柱之間的交叉連接（Main↔Main）。使用者無法獨立調整這兩類角度，且 UI 標籤「Support Bracing Angle」無法清楚表達其物理意義。Profile JSON 已預先寫入 `angle_between_top_and_middle`，但程式尚未接上，需完成參數拆分與 UI 對齊。

## What Changes

- 新增 `angle_between_top_and_middle`（UI：**Angle Between Top And Middle**），控制 Top↔Middle 橋接坡度，預設 45°、上限 90°
- 新增 `cross_angle`（UI：**Cross Angle**），控制 Main↔Main 主柱交叉連接坡度，預設 45°、上限 90°
- Main 區塊：將原 `support_bracing_angle` 欄位改為顯示 `angle_between_top_and_middle`
- Bridge 區塊：在 `support_critical_angle`（Support Angle）下方新增 `cross_angle`
- `SupportTreeConfig` 將單一 `bridge_slope` 拆分為 `top_middle_slope` 與 `cross_slope`，Default Tree 演算法依場景使用對應角度
- 保留 `support_bracing_angle` key 作為向後相容 alias（讀取時 fallback 至兩個新參數；舊 profile 遷移）
- 手動支撐點維持全域 preset 行為，不新增 per-point 角度調整
- **範圍限制**：僅 Default Tree；Branching Tree 的 `branchingsupport_bracing_angle` 不在本次範圍

## Capabilities

### New Capabilities

- `sla-support-angle-split`: 定義 Top↔Middle 與 Main↔Main 角度參數的 UI 佈局、config key、引擎映射、向後相容策略及切片行為

### Modified Capabilities

- `sla-support-param-wiring`: 新增角度參數須透過 `make_support_cfg()` 傳入支撐引擎，且變更時觸發支撐樹重算

## Impact

- **Config 層**：`PrintConfig.hpp/cpp`（`SLAPrintObjectConfig` 新增 `angle_between_top_and_middle`、`cross_angle`；保留 `support_bracing_angle` alias）
- **引擎層**：`SupportTree.hpp`、`SLAPrint.cpp`、`SupportTreeBuildsteps.cpp`、`SupportTreeUtils.hpp`
- **UI 層**：`Tab.cpp`（Main / Raft Setting 區塊重排）
- **Preset**：`Preset.cpp`（新 key 登記）
- **Profile**：`resources/profiles/PhrozenSLA/process/*.json`（補 `cross_angle`）
- **i18n**：新增 UI 字串
- **不修改**：`GLGizmoSlaSupports.cpp`（手動點）、`SupportPoint.hpp`、3MF 序列化、Branching Tree 路徑
