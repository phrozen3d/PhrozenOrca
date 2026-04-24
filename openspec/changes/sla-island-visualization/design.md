## Context

SLA 切片流程在 `slaposObjectSlice` 末尾的 `prepare_for_generate_supports()` 中，已透過跨層幾何交集計算建立 `LayerPart.prev_parts` 連結。`prev_parts.empty() == true` 即代表該輪廓與下層任何幾何無交集——這就是 island。

這份資訊在 `slaposSupportPoints` 的 `generate_support_points()` 中被消費，但從未對外暴露給 UI 層。現有 `GLGizmoSlaSupports` 只顯示支撐點球，使用者無法看到哪些區域觸發了 island 判斷。

入口：擴充現有 `GLGizmoSlaSupports`（非 `GLGizmoLcdOverhangDetection`），因為 SLA 支撐操作的主要工作流程在此。

## Goals / Non-Goals

**Goals:**
- 在 `slaposSupportPoints` 完成後提取 island 輪廓並存入 `SLAPrintObject`，對外暴露給 UI 層
- 在 `GLGizmoSlaSupports` 的 `on_render()` 中渲染半透明 island 輪廓疊加層
- 提供 Checkbox 切換 overlay 顯示、可點擊 island 清單、Camera Focus 至選中 island
- 允許為選定 island 一鍵生成支撐點並觸發支撐樹重建
- 不破壞現有 `slaposSupportPoints` / `slaposSupportTree` 演算法

**Non-Goals:**
- 保留舊版 pixel rasterization island 偵測（`Island_Detector.hpp/.cpp`）——完整移除
- 修改 `SupportPointGenerator` 的演算法邏輯
- 持久化 island 支撐點至 3mf 或專案檔案（session-only）
- FDM 機種支援
- 非同步偵測（island 資料在 step 完成時已同步可得）

## Decisions

### D0：完整移除舊版 pixel rasterization island 偵測

舊版 `Island_Detection()`（位於 `GLGizmoLcdOverhangDetection`）透過將切片圖像轉換為 bitmap，再執行影像處理偵測孤島。新版直接讀取 `LayerPart.prev_parts.empty()`，速度更快、精度更高且不依賴 rasterization 步驟。

兩套機制完全平行，保留舊版只會造成混淆。移除 `GLGizmoLcdOverhangDetection` 中的三個 island 方法及成員，並刪除 `libslic3r/IslandDetection/` 整個目錄。`GLGizmoLcdOverhangDetection` 其餘的 overhang 偵測功能不受影響。

### D1：提取位置選擇 `slice_model()` 而非 `support_points()`（Phase 5.5 修訂）

**初始設計（Phase 1）**：在 `support_points()` 末段提取 island 輪廓。

**修訂原因（Phase 5.5）**：實作驗證後發現，island 提取必須在 `slaposSupportPoints`（auto-generate support）完成後才能取得資料，使用者必須先跑一次 auto-generate support 才能使用 island detection 功能。這與預期的獨立工作流程衝突。

**新決策**：將 island 提取移至 `slice_model()` 中，緊接在 `prepare_for_generate_supports()` 之後，並保留在既有的 `if (generate_support)` 條件內：
- `generate_support = false`（預設）：跳過，零額外成本，island 功能不可用
- `generate_support = true` + 切片完成：island 資料立即可用，不需再等 auto-generate support 執行

`prepare_for_generate_supports()` 在此步驟已建立完整的 `SupportPointGeneratorData`（含 `prev_parts` 連結），island 提取直接讀取，無重複計算。

替代方案：
- 建立獨立輕量版（只跑步驟 1+2）→ 需實作新函式且維護兩條程式碼路徑，成本較高
- 無條件呼叫 `prepare_for_generate_supports()` → 對所有使用者增加切片成本，不適合（預設 `generate_support=false`）

### D2：資料儲存在 `SLAPrintObject` 而非 `SupportPointGeneratorData`

`SupportPointGeneratorData` 是 step 內部物件，生命週期短且對 GUI 不可見。將 `IslandContourSet` 作為 `SLAPrintObject` 的成員，Gizmo 可直接存取，且 step invalidate 時可同步清除。

### D3：Targeted Support 注入手動支撐點而非觸發 `slaposSupportPoints` 重跑

直接呼叫 `uniform_support_island()` 取得支撐點位置，以 `SupportPointType::island` 加入 `sla_support_points`，再觸發 `slaposSupportTree` 重建。這樣不重跑全部自動生成，保留使用者現有的手動支撐點設定。

替代方案：重跑 `slaposSupportPoints` → 放棄，會清除使用者的手動調整。

### D4：Camera Focus 整合至現有 Overhang Area 導覽 UI，不新建 UI 元件

`GLGizmoLcdOverhangDetection` 已有 `< [current / total] >` 導覽列（標籤 "Overhang Area"，成員 `m_current_overhang_area_index` / `m_total_overhang_areas`）。整合策略：
- `sync_island_data()` 在進入 Gizmo 或收到 `RELOAD_SLA_SUPPORT_POINTS` 時，將 `m_total_overhang_areas` 更新為 `IslandContourSet.islands.size()`
- `<##overhang` / `>##overhang` 按鈕 handler 在更新 index 後立即呼叫 `focus_camera_on_island()`
- `focus_camera_on_island()` 計算選中 island ExPolygon 的 AABB（`unscale()` + Z ±2mm + `po->trafo()`），呼叫 `m_parent.zoom_to_box()`

不另建 `ImGui::BeginChild()` 清單。現有 `"%d/%d"` 顯示格式不變，只是資料來源從舊的 hardcode / pixel 偵測改為 `IslandContourSet`。

### D5：Island overlay 的可見性由 Gizmo 狀態控制，不提供 Checkbox 開關

overlay 的顯示與否完全由 `on_set_state(On/Off)` 決定：進入 Gizmo 且有偵測資料時自動顯示，離開 Gizmo 時立即隱藏並釋放 GL 資源。

不提供 Checkbox 的理由：island 輪廓是偵測結果的直接呈現，進入 Gizmo 就代表使用者在操作支撐工作流程，overlay 應始終可見；額外的開關只增加操作步驟，不帶來明確收益。

`on_render()` 本身僅在 Gizmo active 時被 framework 呼叫，因此 `render_island_overlay()` 不需要任何可見性守衛，只需判斷 `m_island_data.valid` 是否有資料可渲染。

GL 渲染方面：island 輪廓疊加在模型表面（Z +0.05mm offset），`glDisable(GL_DEPTH_TEST)` 確保輪廓在任何視角下不被模型幾何遮蔽。高亮選中 island 使用額外 +0.05mm（總計 +0.10mm）避免 Z-fighting。

## Risks / Trade-offs

- **`generator_data` 的 scope**：`support_points()` 內 `generate_support_points()` 之後，`generator_data.layers` 是否仍在 scope 內有效，需確認局部變數生命週期。→ 在 `generate_support_points()` 回傳前讀取，確認 `layers` 為 value 而非 dangling reference。
- **座標單位轉換**：`ExPolygon::Point` 為 scaled 整數座標，渲染和 AABB 計算前必須 `unscale()`，否則幾何位置錯誤。→ 所有從 `LayerPart.shape` 讀取後立即轉換。
- **Targeted Support 重複點**：重複點擊「Add Support」會累積重複的支撐點。→ 生成前先移除 `SupportPointType::island` 且 Z 值對應目標 island 的既有點。
- **Step invalidate 時序**：模型修改觸發 `slaposSupportPoints` invalidate 後，若 Gizmo 仍開啟，`m_island_data` 需同步清除。→ Task 1.3 在 invalidate 路徑上呼叫 `clear_island_contours()`，Gizmo 在下次 `sync_island_data()` 時讀到 `valid == false`。
- **多 ModelObject 場景**：初期僅處理當前選中的單一 ModelObject，不跨物件混用 island 資料。→ 後續可擴充為 per-object state。

##架構總覽

```
SLAPrintSteps.cpp (per SLAPrintObject)
  slice_model()
    if (generate_support)
      prepare_for_generate_supports()      ← 現有，建立 SupportPointGeneratorData
      [NEW] extract island contours        ← Phase 5.5：讀取 layers，找 prev_parts.empty()
      po.set_island_contours()             ← 存入各自的 SLAPrintObject

  support_points()
    generate_support_points()              ← 現有，不修改（不再提取 island）

GLGizmoLcdOverhangDetection.cpp           （所有 island UI 功能均在此）

  on_set_state(On)
    sync_all_objects_names()               ← m_model_names 填入所有場景 SLA 物件名稱
    m_current_model_index = selected_obj   ← 對應進入前選取的物件
    若有資料 → focus_camera_on_island(0)

  on_render()
    render_island_contours()               ← overlay(所有可見islands) + highlight(當前)

  on_render_input_window()
    < [ModelName] > (model nav)            ← 切換 m_current_model_index
                                             → rebuild_overhang_area_index_map(false)
    < [N/Total] > (overhang nav)           ← 切換 m_current_overhang_area_index
                                             → focus_camera_on_island()
                                             → rebuild_island_highlight_mesh()
    [Detect Selected]                      ← sync_island_data_for_object(current)
                                             → rebuild_overhang_area_index_map(false)
    [Detect All]                           ← sync_island_data_for_all()
                                             → rebuild_overhang_area_index_map(true)
    [Add Overhang Supports]                ← generate_island_support_points()
                                             （對 m_overhang_area_index_map 內所有 islands）

  Key data structures:
    m_island_data_per_object               ← map<obj_idx, IslandContourSet>
    m_overhang_area_index_map              ← vec<(obj_idx, island_idx)> flat index
    m_total_overhang_areas                 ← = m_overhang_area_index_map.size()
```

## 渲染規格

| 物件 | 顏色 RGBA | Z Offset | GL 狀態 |
|------|-----------|----------|---------|
| Island overlay（非選中）| `(1.0, 0.85, 0.2, 0.4)` | +0.05 mm | depth test off |
| Island overlay（選中高亮）| `(1.0, 0.5, 0.0, 0.75)` | +0.10 mm | depth test off |
