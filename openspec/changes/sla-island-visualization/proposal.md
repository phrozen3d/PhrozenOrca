## Why

SLA 切片流程已能識別孤島（island）——某一層與下方幾何完全無交集的懸空區域——但這些位置只埋藏在演算法內部，使用者完全無法得知，也無法針對特定孤島直接採取行動。現有的支撐工作流程要求使用者憑經驗手動判斷，缺乏可視性且效率低落。

## What Changes

- 移除舊版 `GLGizmoLcdOverhangDetection` 的 pixel rasterization island 偵測流程（`Island_Detection()`、`rebuild_island_models()`、`render_island_contours()` 及整個 `libslic3r/IslandDetection/` 模組）
- 在 `GLGizmoSlaSupports`（SLA 支撐 Gizmo）中新增 island 偵測資料提取，自動生成支撐點後將 island 輪廓（Z + ExPolygon）存入 `SLAPrintObject`
- 新增半透明彩色 island 輪廓疊加層渲染（亮黃橙色，alpha 40%，疊加於模型表面）
- 新增 Checkbox UI 元件，允許使用者自由切換 island overlay 的顯示狀態
- 新增 island 清單面板與 Camera Focus 機制，點擊清單項目自動移動鏡頭至該 island 的 3D 包圍盒
- 新增目標支撐生成功能，為選定 island 或全部 island 呼叫 `uniform_support_island()` 並將結果注入手動支撐點清單，觸發支撐樹重建

## Capabilities

### New Capabilities

- `sla-island-contour-overlay`: 自動生成完成後提取 island 輪廓並渲染為模型表面的半透明彩色多邊形疊加層，含小 island 過濾與重新生成後自動更新
- `sla-island-visibility-toggle`: Gizmo 面板 Checkbox，允許顯示或隱藏 island 輪廓覆蓋層
- `sla-island-camera-focus`: Island 清單 UI，點擊項目後 Camera 自動對焦並縮放至選中 island 的 3D 包圍盒，並以高亮色區分選中 island
- `sla-island-targeted-support`: 為選定 island（或全部）生成支撐點並注入現有手動支撐點清單，觸發 `slaposSupportTree` 重建

### Modified Capabilities

## Impact

- **新增/修改檔案**：`src/libslic3r/SLAPrint.hpp`、`src/libslic3r/SLAPrint.cpp`、`src/libslic3r/SLAPrintSteps.cpp`、`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp`、`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`
- **不修改**：`SupportPointGenerator.hpp/.cpp`（僅呼叫現有 `uniform_support_island()`）、`SLAPrintSteps.cpp` 的切片演算法、任何 FDM 程式碼、PhrozenOrca 自訂功能
- **依賴**：`GLCanvas3D::zoom_to_box()`（現有 API）、`triangulate_expolygon_3d()`（現有工具函式）、`uniform_support_island()`（現有函式）
