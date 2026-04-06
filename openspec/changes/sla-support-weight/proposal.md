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
- `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` — `filterfn` 中根據 weight 縮放 `back_r`
- `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` / `.hpp` — 新增重量選擇 UI
- `src/libslic3r/Format/3mf.cpp` / `bbs_3mf.cpp` / `AMF.cpp` — 序列化 weight 欄位

**不需修改：**
- `SupportTreeConfig` 或其他全域參數
- FDM 支撐程式碼
- PhrozenOrca 自訂功能
