## Why

Phrozen Orca Resin 支撐點 Gizmo 的 Light / Middle / Heavy 三檔快速預設，其尺寸與使用者熟悉的 CHITUBOX 不一致，且目前「接觸深度」與「段長度」兩項在三檔之間並未隨檔位變化（為固定值）。為降低 CHITUBOX 使用者的轉換成本、並讓三檔預設呈現真正符合直覺的尺寸階梯，需將六項支撐尺寸預設值對齊 CHITUBOX 的 Light/Middle/Heavy。

## What Changes

- **L/M/H 三檔預設值改為 CHITUBOX 對照值**（六項支撐尺寸）：

  | 參數 | Light | Middle | Heavy |
  |---|---|---|---|
  | 接觸直徑 `support_contact_diameter` | 0.5 | 0.8 | 1.0 |
  | 接觸深度 `support_head_penetration` | 0.3 | 0.4 | 0.6 |
  | 上端直徑 `support_head_front_diameter` | 0.3 | 0.4 | 0.6 |
  | 較低直徑 `support_head_back_diameter` | 0.8 | 1.2 | 1.5 |
  | 段長度 `support_segment_length` | 2 | 2 | 3 |
  | 支撐柱直徑 `support_pillar_diameter` | 0.8 | 1.2 | 1.5 |

- **新增兩檔分檔能力**：`support_head_penetration`（接觸深度）與 `support_segment_length`（段長度）由原本的固定值，改為隨 L/M/H 檔位變動。`apply_weight_preset` 於套用預設時一併寫入這兩個參數。
- **同步 Phrozen SLA 系統 process 預設**：將 profile JSON 初始值調整為對齊新的 Middle 檔（柱徑 1.2、段長度 2、較低直徑 1.2），確保載入預設時 Gizmo 反查邏輯正確以 Middle 高亮、且顯示值與 Middle 一致。
- **Bump Phrozen SLA vendor 版號**（patch 級：`01.00.05` → `01.00.06`），使既有使用者也能收到更新後的系統預設。
- 非破壞性變更；不影響已存在的使用者自訂 preset（不做值遷移）。

## Capabilities

### New Capabilities
- （無）

### Modified Capabilities
- `sla-weight-preset`: L/M/H 切換所套用的支撐尺寸由「6 個參數」擴充為「涵蓋接觸深度與段長度」的集合，且各檔具體數值對齊 CHITUBOX；反查高亮仍以 `support_pillar_diameter` 為鍵，需與新的系統預設柱徑一致。

## Impact

- **程式碼**：
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — `SupportWeightPreset` struct 擴充兩欄位、`k_weight_presets` 三檔數值更新、`apply_weight_preset` 新增寫入 `support_head_penetration` 與 `support_segment_length`。
- **資源 / Profile**：
  - `resources/profiles/PhrozenSLA/process/sla_print_common.json` — `support_pillar_diameter`、`support_segment_length`、`support_head_back_diameter` 對齊 Middle。
  - `resources/profiles/PhrozenSLA/process/*.json`（6 個子 process）— `support_pillar_diameter` 對齊 1.2。
  - `resources/profiles/PhrozenSLA.json` — `version` patch bump（觸發既有使用者的 profile 更新）。
- **不影響**：`PrintConfig.cpp` 硬編碼預設（由 profile 覆蓋）、`branchingsupport_*` 分枝支撐系列、`support_head_width`（不顯示於 Support 分頁且對預設支撐幾何無作用）。