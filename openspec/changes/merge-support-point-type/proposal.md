## 為何

PhrozenOrca 目前以 `bool is_new_island` 儲存 SLA 支撐點分類，只能區分 island 與非 island 兩種狀態。PrusaSlicer 已將此演進為三值的 `SupportPointType` 列舉（`manual_add`、`island`、`slope`），提供更清晰的 UI 回饋、精確的統計資訊，以及清楚的拖曳升格語意。此 merge 是後續 `SupportWeight`（輕中重）功能的必要基礎——該功能需要 `manual_add` 作為 per-point 粗細控制的判斷依據。

## 變更內容

- 將 `SupportPoint` struct 中的 `bool is_new_island` 替換為 `SupportPointType type` 列舉（`manual_add`、`island`、`slope`）
- 更新 `SupportPointGenerator`，在建立自動生成點時指派 `SupportPointType::island` 和 `SupportPointType::slope`
- 更新 `GLGizmoSlaSupports`，以三種不同顏色渲染（手動 / island / slope）並顯示各類型統計
- 拖曳任何自動生成的點後，該點升格為 `manual_add`（用戶自行承擔責任）
- 保留 `lock_unique_islands` 行為：改用 `is_island()` 謂詞取代 `is_new_island` bool
- **BREAKING**：`SupportPoint` 序列化格式變更——3mf/bbs_3mf/AMF 必須處理從舊 float 編碼（`0.0`/`1.0`）到新類型編碼（`0`/`1`/`2`）的遷移

## 功能範疇

### 新增功能

- `support-point-type`：SLA 支撐點的三值分類（manual_add / island / slope），含 UI 顏色區分、統計顯示、拖曳升格行為

### 修改功能

- （無——不影響任何現有 openspec spec）

## 影響範圍

**修改的檔案：**
- `src/libslic3r/SLA/SupportPoint.hpp` — struct 定義、建構子、序列化
- `src/libslic3r/SLA/SupportPointGenerator.cpp` — 生成時的類型指派
- `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp` — 顏色渲染、統計、拖曳邏輯
- `src/libslic3r/Format/3mf.cpp` — 序列化 + 遷移處理
- `src/libslic3r/Format/bbs_3mf.cpp` — 序列化 + 遷移處理
- `src/libslic3r/Format/AMF.cpp` — 序列化 + 遷移處理

**不需修改：**
- `SupportTreeBuildsteps.cpp` — tree builder 不使用 `type`
- FDM 相關程式碼 — 完全不受影響
- PhrozenOrca 自訂功能（`BUILD_PHROZEN_ORCA`、`PartPlateList` 等）
