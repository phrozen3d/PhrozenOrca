# Proposal：SLA 孤島偵測視覺化與局部支撐生成

## 背景

SLA 切片過程中，「island」是指某一層的 ExPolygon 輪廓與下方任何幾何均無交集的懸空區域。目前 PhrozenOrca 的自動支撐生成流程（`slaposSupportPoints`）會在這些 island 上放置支撐點，但使用者無法看到哪些區域觸發了 island 判斷，也無法針對特定模型的 island 區域單獨執行支撐生成。

現有的 `GLGizmoLcdOverhangDetection` 已實作基礎的孤島偵測雛形（commit `9a07fe61b9`），但其偵測精度固定、缺乏模型切換能力、無法針對 island 局部生成支撐樹，且與主切片流程高度耦合。

## 問題陳述

1. **使用者不可見**：island 偵測結果埋藏在演算法內部，使用者無法得知哪些區域需要支撐
2. **無法局部操作**：現有流程只能全模型自動生成，無法針對 island 獨立執行支撐樹建構
3. **精度固定**：偵測解析度無法由使用者調整，高精度模型與快速預覽無法區分
4. **耦合過深**：舊版偵測流程直接依賴完整切片管線，難以獨立控制

## 提案解決方案

將 `GLGizmoLcdOverhangDetection` 擴充為完整的「孤島偵測視覺化 + 局部支撐生成」工作流程：

1. **Island 偵測服務**（`IslandDetectionService`）：從切片資料建構跨層連通性，識別 `prev_parts.empty()` 的 island 區域，輸出每層的 ExPolygon 輪廓，不干擾主切片流程
2. **輪廓視覺化**（`IslandVisualizationRenderer`）：將 island ExPolygon 三角化為 3D mesh，疊加渲染在模型表面
3. **局部支撐生成**（`IslandSupportGenerator`）：僅對 island 區域執行支撐點生成，不調用完整自動支撐管線
4. **支撐樹建構**（`TreeSupportBuilder`）：對局部支撐點執行 `SupportTreeBuildsteps`，產出支撐 mesh 並渲染

## 設計原則

- **不破壞現有管線**：`slaposSupportPoints` 的完整自動支撐流程完全不修改
- **保留舊版偵測**：commit `9a07fe61b9` 的實作保留，不刪除，作為對照與除錯基準
- **模組化隔離**：四個子模組各有獨立職責，可單獨測試與替換
- **優先複用**：`prepare_generator_data()`、`uniform_support_island()`、`SupportTreeBuildsteps` 直接複用，不重寫
- **SLA 專用**：所有新增程式碼均受 printer technology 條件限制，FDM 流程不受影響

## 功能範疇

### 新增功能

- `island-detection-service`：獨立的孤島偵測服務，可對指定模型執行，不觸發完整切片
- `island-visualization`：island 輪廓在 3D view 的彩色半透明 mesh 覆蓋層
- `island-support-generator`：針對 island 區域的局部支撐點生成
- `island-tree-support`：對局部支撐點建構支撐樹並渲染

### UI 入口

- `GLGizmoLcdOverhangDetection`（擴充現有 gizmo）

### 不在範疇內

- 修改 `slaposSupportPoints` 或 `slaposSupportTree` 主切片流程
- 刪除舊版偵測實作（commit `9a07fe61b9`）
- 支撐點序列化（僅 session 有效，不持久化）
- FDM 機種支援

## 影響範圍

**新增或擴充的檔案：**
- `src/slic3r/GUI/Gizmos/GLGizmoLcdOverhangDetection.hpp` / `.cpp` — UI、偵測觸發、渲染協調
- `src/libslic3r/SLA/IslandDetectionService.hpp` / `.cpp` — 新增：island 偵測服務
- `src/libslic3r/SLA/IslandSupportGenerator.hpp` / `.cpp` — 新增：局部支撐生成

**複用但不修改的元件：**
- `SupportPointGenerator.cpp` — `prepare_generator_data()` 直接呼叫
- `SupportPointGenerator.hpp` — `uniform_support_island()` 直接呼叫
- `SupportTreeBuildsteps.cpp` — `SupportTreeBuildsteps` 直接複用
- `GLModel` — mesh 渲染

**不修改：**
- `SLAPrintSteps.cpp` / `SLAPrint.hpp` — 主切片流程完全不動
- FDM 程式碼
- PhrozenOrca 自訂功能
