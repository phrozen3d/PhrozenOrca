## 為何

目前 SLA 手動支撐模式中，所有支撐點使用相同的全域參數（`head_back_radius_mm`）決定柱子粗細。使用者在不同區域需要不同強度的支撐時，只能修改全域設定，無法針對單一支撐點調整。新增輕/中/重三檔的 per-point 重量選項，讓使用者在放置手動支撐時精細控制每根支撐的粗細。

## 變更內容

- 新增 `SupportWeight` 列舉（`Light`、`Medium`、`Heavy`）至 `SupportPoint`
- 預設值為 `Medium`（對應現有行為，backward compatible）
- 僅 `type == manual_add` 的支撐點會套用 weight 縮放；自動生成點（`island`、`slope`）忽略此欄位
- 在 `SupportTreeBuildsteps` 的 `filterfn` 中，根據 weight 縮放 `back_r`（`Light=0.6x`、`Medium=1.0x`、`Heavy=1.4x`）
- GLGizmo 面板新增 Light/Medium/Heavy 選擇器，影響後續點擊放置的點
- 3mf 序列化：將 weight 編碼進 `head_front_radius` 的小數部分（避免增加序列化欄位數）或新增第六個浮點數
- **依賴**：需先完成 `merge-support-point-type`（依賴 `SupportPointType::manual_add` 作為 weight 的啟用條件）

## 功能範疇

### 新增功能
- `support-weight-selection`：使用者在手動支撐模式下可選擇輕/中/重，控制 per-point 柱子粗細

### 修改功能
- `support-point-type`（來自 `merge-support-point-type`）：`SupportPoint` struct 新增 `SupportWeight weight` 欄位

## 影響範圍

**修改的檔案：**
- `src/libslic3r/SLA/SupportPoint.hpp` — 新增 `SupportWeight` 列舉與 `weight` 欄位
- `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` — `filterfn` 中根據 weight 縮放 `back_r`；`create_ground_pillar` 呼叫端根據 weight 傳入 `allow_widening` 旗標
- `src/libslic3r/SLA/SupportTreeBuildsteps.hpp` — `create_ground_pillar` 新增 `bool allow_widening = true` 參數
- `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` / `.hpp` — 新增重量選擇 UI
- `src/libslic3r/Format/3mf.cpp` / `bbs_3mf.cpp` / `AMF.cpp` — 序列化 weight 欄位（暫緩）

**不需修改：**
- `SupportTreeConfig` 或其他全域參數
- FDM 支撐程式碼
- PhrozenOrca 自訂功能

## 實作備註

### Light 柱子加寬問題（實測發現）

`create_ground_pillar()` 內有一段「細柱自動加寬」邏輯：當柱子半徑小於全域 `head_back_radius_mm` 且高度超過 `20 * radius` 時，系統會自動插入 `DiffBridge` 將柱子漸寬至全尺寸，以確保結構強度。

此行為導致 Light 點雖然接觸頭（Head）正確縮小，但支撐柱到底盤的部分仍被強制加寬回全尺寸。

**修法：** 在 `create_ground_pillar()` 新增 `bool allow_widening = true` 參數。所有既有呼叫點不變（預設 `true`）。Light weight 點的呼叫傳入 `allow_widening = false`，跳過加寬邏輯，讓整根柱子維持縮小後的半徑。修改的呼叫點：
- `make_pillar_only_heads()` 的主要呼叫
- 同函式內 cluster sidepoint fallback 呼叫
- `connect_to_ground()` 的呼叫

Light 點在特別高的模型上可能因無法找到支撐路徑而回退至 `m_iheads_onmodel`（log warning），屬預期行為。
