## 為何

SLA 自動支撐生成完成後，使用者無法看到模型表面哪些區域屬於「island」（即前一層沒有支撐的懸空區域）。支撐點雖會自動放置在這些區域，但使用者看不到觸發支撐的底層幾何形狀。將 island 輪廓直接視覺化在模型表面，可讓使用者了解支撐點放置的依據、識別支撐過多或不足的區域，並在列印前做出更明智的手動調整。

## 變更內容

- 自動支撐生成完成後，從 `SupportPointGeneratorData.layers` 中計算 island 輪廓，並以彩色覆蓋多邊形投影至模型表面
- 在 `GLGizmoSlaSupports` 中以半透明彩色網格層渲染輪廓（不是支撐點，而是 2D 區域覆蓋）
- Island 輪廓僅在 SLA 支撐 gizmo 啟用且已執行過自動生成時顯示
- 基本功能不需新增使用者設定；輪廓可見性可透過 gizmo 面板中的核取方塊切換

## 功能範疇

### 新增功能

- `sla-island-contour-overlay`：偵測到的 island 區域的視覺化覆蓋層，以彩色輪廓投影至模型表面，顯示在 SLA 支撐 gizmo 中

### 修改功能

- （無）

## 影響範圍

**修改的檔案：**
- `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` / `.hpp` — 渲染 island 輪廓覆蓋網格
- `src/libslic3r/SLA/SupportPointGenerator.hpp` / `.cpp` — 從 `SupportPointGeneratorData` 中輸出 island 輪廓資料
- `src/libslic3r/SLAPrintSteps.cpp` — 將 island 輪廓資料傳遞至 gizmo（或儲存在 `SLAPrintObject` 上）
- `src/libslic3r/SLAPrint.hpp` — 可能在 `SLAPrintObject` 上儲存 island 輪廓

**不需修改：**
- `SupportTreeBuildsteps.cpp` — 樹狀支撐建構不受影響
- `SupportPoint` struct — 不需修改資料模型
- FDM 程式碼 — 完全不受影響
- PhrozenOrca 自訂功能
