## 0. 移除舊版 Island 偵測流程

舊版使用 pixel rasterization 方式（將切片圖像轉換為 bitmap 後執行影像偵測），位於 `GLGizmoLcdOverhangDetection` 及 `libslic3r/IslandDetection/`，與新版的 `LayerPart.prev_parts` 幾何分析方式完全不同。移除舊流程，消除維護負擔與概念混淆。

- [x] 0.1 在 `GLGizmoLcdOverhangDetection.hpp` 中移除：`#include "libslic3r/IslandDetection/Island_Detector.hpp"`、`Island_Detection()` / `rebuild_island_models()` / `render_island_contours()` 方法宣告、`m_detected_islands`（`std::vector<Slic3r::island::Island>`）及 `m_island_models`（`std::vector<GLModel>`）成員
- [x] 0.2 在 `GLGizmoLcdOverhangDetection.cpp` 中移除：`render_island_contours()` 呼叫（約 line 189）、`Island_Detection()` 呼叫（約 line 437）、`m_detected_islands.clear()` / `m_island_models.clear()` 的清除邏輯（約 lines 102–103）、`write_json()` static 函式（約 line 923）、`Island_Detection()` / `rebuild_island_models()` / `render_island_contours()` 三個方法完整實作（約 lines 944–1181）
- [x] 0.3 刪除整個 `src/libslic3r/IslandDetection/` 目錄（含 `Island_Detector.hpp`、`Island_Detector.cpp`、`main.cpp`）
- [x] 0.4 在 `src/libslic3r/CMakeLists.txt` 中移除 `IslandDetection/Island_Detector.hpp` 和 `IslandDetection/Island_Detector.cpp` 兩個編譯項目（約 lines 510–511）

**Phase 0 驗收**：編譯無錯誤，`GLGizmoLcdOverhangDetection` 的其餘功能（overhang 角度偵測、面板 UI）正常運作；「Detect Selected」按鈕點擊後無 crash（handler 暫時為空）。

---

## 1. Backend：Island 資料結構與提取

- [x] 1.1 在 `SLAPrint.hpp` 的 `namespace sla` 中定義 `IslandContour` struct（`float print_z`、`ExPolygon contour`、`float area`）和 `IslandContourSet` struct（`std::vector<IslandContour> islands`、`bool valid = false`）
- [x] 1.2 在 `SLAPrintObject` class 中新增 `sla::IslandContourSet m_island_contours` private 成員，及 `island_contours()` const accessor、`set_island_contours()` setter、`clear_island_contours()` 方法
- [x] 1.3 在 `SLAPrintSteps.cpp` 的 `support_points()` 函式中，於 `generate_support_points()` 呼叫完成後迭代 `generator_data.layers`，收集 `part.prev_parts.empty()` 的 part，過濾面積 < 閾值的微小 island，組成 `IslandContourSet` 並呼叫 `po.set_island_contours()`；加入 `BOOST_LOG_TRIVIAL(info)` 輸出 island 數量和 Z 高度供驗證
- [x] 1.4 在 `SLAPrint.cpp` 中找到 `slaposSupportPoints` 被 invalidate 的所有路徑，加入 `po.clear_island_contours()` 確保資料失效時同步清除

**Phase 1 驗收**：執行 SLA 自動生成支撐後，console 輸出每個 object 的 island 數量與 Z 值；切片輸出（支撐點數量、支撐樹形狀）與修改前完全相同。

---

## 2. Gizmo 資料層：多物件 Island 資料管理

`GLGizmoLcdOverhangDetection` 需要能追蹤場景中所有 `SLAPrintObject` 的 island 資料，並維護一份扁平化的 overhang area 索引表以供導覽使用。

- [x] 2.1 在 `GLGizmoLcdOverhangDetection.hpp` 中新增成員：
  ```cpp
  // per-object island data
  std::map<int, sla::IslandContourSet> m_island_data_per_object;
  // flat index for overhang area navigation: [(object_idx, island_idx_within_object), ...]
  std::vector<std::pair<int,int>> m_overhang_area_index_map;
  // GL models
  GLModel m_island_overlay_model;
  GLModel m_island_highlight_model;
  bool m_island_data_dirty{false};
  ```
  及方法宣告（`sync_all_objects_names()`、`sync_island_data_for_object(int obj_idx)`、`sync_island_data_for_all()`、`rebuild_overhang_area_index_map(bool all_objects)`、`rebuild_island_overlay_mesh()`、`render_island_contours()`、`rebuild_island_highlight_mesh(int flat_idx)`、`focus_camera_on_island(int flat_idx)`、`generate_island_support_points()`）

- [x] 2.2 實作 `sync_all_objects_names()`：迭代場景中所有 `SLAPrintObject`（透過 `wxGetApp().plater()->sla_print()->objects()`），將每個 `po->model_object()->name` 填入 `m_model_names`；取代舊版的 `mo->volumes` 填法（舊版在 `update_from_model_object()` 中，需移除舊邏輯）

- [x] 2.3 實作 `sync_island_data_for_object(int obj_idx)`：取第 `obj_idx` 個 `SLAPrintObject` 的 `island_contours()`，存入 `m_island_data_per_object[obj_idx]`；若 `valid == false` 亦同樣存入（保留空狀態）

- [x] 2.4 實作 `sync_island_data_for_all()`：對所有 SLAPrintObject 依序呼叫 `sync_island_data_for_object(i)`

- [x] 2.5 實作 `rebuild_overhang_area_index_map(bool all_objects)`：
  - `all_objects == false`：只收集 `m_island_data_per_object[m_current_model_index]` 的 islands，填入 `m_overhang_area_index_map`
  - `all_objects == true`：收集所有 object 的 islands，依 object 順序填入
  - 更新 `m_total_overhang_areas = m_overhang_area_index_map.size()`；重設 `m_current_overhang_area_index = 0`；設 `m_island_data_dirty = true`

**Phase 2 驗收**：`m_model_names` 填入場景所有 SLA 物件名稱（非 volumes）；`m_island_data_per_object` 在偵測後包含正確 object → island 對應關係。

---

## 3. 進入 Gizmo 時自動定位至已選取的 Model

進入 Gizmo 前如果場景已選取了某個 SLA object，進入後應自動將 `m_current_model_index` 設為該 object，並對焦至其第一個 island（若已有偵測資料）。

- [x] 3.1 在 `on_set_state(On)` 中（或 `on_opening()` 內），呼叫 `sync_all_objects_names()` 初始化 model 清單，接著透過 `m_parent.get_selection().get_object_idx()` 取得進入時的選取 object index，將 `m_current_model_index` 設為該值（若 `get_object_idx()` 返回 -1 則維持 0）

- [x] 3.2 在 3.1 之後，若 `m_island_data_per_object[m_current_model_index].valid == true`，呼叫 `rebuild_overhang_area_index_map(false)` 並 `focus_camera_on_island(0)` 對焦至該 model 的第一個 island

**Phase 3 驗收**：場景有兩個 SLA 物件時，選取第二個後進入 Gizmo，model 導覽顯示第二個物件名稱，若已有偵測結果則鏡頭自動對焦至其第一個 island。

---

## 4. GL 視覺化：Island 輪廓 Overlay

- [ ] 4.1 實作 `rebuild_island_overlay_mesh()`：迭代 `m_overhang_area_index_map` 中所有 entry，取對應 object 的 `IslandContour.contour`，呼叫 `triangulate_expolygon_3d(shape, z + 0.05f)`，合併為單一 `indexed_triangle_set`，透過 `GLModel::init_from()` 建立 GPU buffer，顏色 `ColorRGBA(1.0f, 0.85f, 0.2f, 0.4f)`；需套用各 object 的 `po->trafo()` 轉換至世界座標

- [ ] 4.2 實作新版 `render_island_contours()`（取代舊版同名方法）：若 `m_island_data_dirty` 先呼叫 `rebuild_island_overlay_mesh()`；`glDisable(GL_DEPTH_TEST/CULL_FACE)` + `glEnable(GL_BLEND)` → 渲染 overlay model → 若 `m_current_overhang_area_index >= 0 && !m_overhang_area_index_map.empty()` 渲染 highlight model → 還原 GL 狀態

- [ ] 4.3 在 `on_render()` 中恢復 `render_island_contours()` 呼叫（Phase 0 移除了舊版呼叫，此處加回新版）

**Phase 4 驗收**：「Detect All」後所有物件的 island 都出現亮黃橙半透明 overlay；「Detect Selected」後只有當前 model 的 island 顯示；退出 Gizmo 後無殘留渲染。

---

## 5. 「Detect Selected」整合

點擊 Detect Selected 僅偵測當前 `m_current_model_index` 所指向的 model，overhang area 導覽範圍限定在該 model。

- [ ] 5.1 在 `detect_selected` 按鈕 handler 中（取代舊 `Island_Detection()` 呼叫）：
  1. 呼叫 `sync_island_data_for_object(m_current_model_index)`
  2. 呼叫 `rebuild_overhang_area_index_map(false)`（只收集當前 model 的 islands）
  3. 若 `m_total_overhang_areas > 0`，呼叫 `focus_camera_on_island(0)` 自動對焦第一個 island

- [ ] 5.2 在 `detect_selected` 按鈕前加入 disabled 判斷：若當前 model 的 `slaposSupportPoints` step 尚未完成，按鈕顯示為 disabled，tooltip 提示「Run SLA auto-generate first」

**Phase 5 驗收**：場景有兩個物件時，切換至物件 A 點擊「Detect Selected」→ overhang area 只顯示物件 A 的 island 數量；切換至物件 B 點擊「Detect Selected」→ overhang area 只顯示物件 B 的 island；兩次偵測不互相干擾。

---

## 6. 「Detect All」整合

點擊 Detect All 對所有 SLA 物件偵測，overhang area 導覽涵蓋全部物件的 islands。

- [ ] 6.1 在 `detect_all` 按鈕 handler 中（取代現有 `// TODO`）：
  1. 呼叫 `sync_island_data_for_all()`（對所有 SLAPrintObject）
  2. 呼叫 `rebuild_overhang_area_index_map(true)`（收集所有物件的 islands）
  3. 若 `m_total_overhang_areas > 0`，呼叫 `focus_camera_on_island(0)` 對焦第一個 island

- [ ] 6.2 在 `detect_all` 按鈕前加入 disabled 判斷：若任一 SLA object 的 `slaposSupportPoints` 尚未完成，顯示 disabled（或僅對已完成的 object 顯示 enabled——以實作簡易度決定，優先前者）

**Phase 6 驗收**：「Detect All」後 `m_total_overhang_areas` 等於所有物件 island 的總數；`< >` 導覽可連續切換跨物件的 islands；鏡頭正確 focus 到各物件各 island。

---

## 7. Camera Focus 整合（使用現有 Overhang Area 導覽 UI）

- [ ] 7.1 實作 `focus_camera_on_island(int flat_idx)`：從 `m_overhang_area_index_map[flat_idx]` 取得 `(obj_idx, island_idx)`，再從 `m_island_data_per_object[obj_idx].islands[island_idx]` 取 ExPolygon 及 `print_z`；計算 2D AABB（`unscale()` 轉換）→ 建立 3D AABB（Z range `print_z ±2mm`）→ 套用對應 `SLAPrintObject::trafo()` → 呼叫 `m_parent.zoom_to_box(bb3d)`

- [ ] 7.2 在現有 `<##overhang` 按鈕 handler 的 index 遞減後，加入 `focus_camera_on_island(m_current_overhang_area_index)` 及 `rebuild_island_highlight_mesh(m_current_overhang_area_index)` 呼叫

- [ ] 7.3 在現有 `>##overhang` 按鈕 handler 的 index 遞增後，加入 `focus_camera_on_island(m_current_overhang_area_index)` 及 `rebuild_island_highlight_mesh(m_current_overhang_area_index)` 呼叫

- [ ] 7.4 實作 `rebuild_island_highlight_mesh(int flat_idx)`：從 `m_overhang_area_index_map[flat_idx]` 取 ExPolygon，建立 `m_island_highlight_model`，Z offset `+0.10f`，顏色 `ColorRGBA(1.0f, 0.5f, 0.0f, 0.75f)`，套用對應 object 的 `trafo()`

- [ ] 7.5 在現有 model `<` 按鈕 handler 的 index 遞減後，呼叫 `rebuild_overhang_area_index_map(false)` 以切換 overhang area 範圍至新的 current model（僅顯示該 model 已偵測的 islands，若未偵測則清空）

- [ ] 7.6 在現有 model `>` 按鈕 handler 的 index 遞增後，同 7.5

**Phase 7 驗收**：「Detect All」後，`< >` 導覽跨越兩個物件的 island 時，鏡頭正確切換到不同物件上；切換 model 後 overhang area 範圍自動更新為新 model 的 islands；選中 island 以亮橙高亮。

---

## 8. 「Add Overhang Supports」整合

點擊按鈕對當前 overhang area 索引表中的所有 island（即當前偵測範圍）生成支撐點，**不新建按鈕**。

- [ ] 8.1 實作 `generate_island_support_points()`：迭代 `m_overhang_area_index_map`，依 `(obj_idx, island_idx)` 取對應 `SLAPrintObject` 和 `IslandContour`；先移除該 object 現有的 `SupportPointType::island` 點，再呼叫 `uniform_support_island(contour, {}, cfg.island_configuration)`，轉換後加入各 object 的 `sla_support_points`；完成後對有新增點的所有 object 觸發 `slaposSupportTree` 重建

- [ ] 8.2 在 `add_overhang_supports` 按鈕 handler 的 TODO 位置呼叫 `generate_island_support_points()`；若 `m_overhang_area_index_map.empty()` 則 disabled（確認現有第三個參數正確傳入）

**Phase 8 驗收**：「Detect Selected」→「Add Overhang Supports」→ 只有當前 model 生成支撐；「Detect All」→「Add Overhang Supports」→ 所有物件都生成支撐；重複點擊不累積重複點；手動支撐點不受影響。

---

## 9. 邊界條件防護

- [ ] 9.1 在 `on_set_state(Off)` 中 reset `m_island_overlay_model`、`m_island_highlight_model`，清空 `m_island_data_per_object`、`m_overhang_area_index_map`，重設 `m_current_overhang_area_index = 0`、`m_total_overhang_areas = 0`
- [ ] 9.2 在 `sync_island_data_for_object()` 入口確認 obj_idx 合法（在 SLAPrintObject 數量範圍內）
- [ ] 9.3 `m_overhang_area_index_map.empty()` 時，overhang `< >` 按鈕 handler 不呼叫 focus（邊界保護）
- [ ] 9.4 場景物件數為 0（空場景）時，model `< >` 按鈕 handler 不做任何操作

---

## 10. Regression 驗證

- [ ] 10.1 SLA 自動生成支撐點的數量和位置與修改前完全相同
- [ ] 10.2 手動新增/刪除支撐點功能正常，不受 island 邏輯干擾
- [ ] 10.3 未點擊「Add Overhang Supports」時，支撐樹形狀與修改前完全相同
- [ ] 10.4 切片輸出（sl1/zip）的 PNG 圖像與修改前完全相同
- [ ] 10.5 FDM 機種下開啟 Gizmo 無渲染錯誤、無 crash
- [ ] 10.6 單物件場景：行為與修改前一致（model 導覽只有一個物件，overhang area 正常運作）
- [ ] 10.7 多物件場景：Detect Selected 與 Detect All 結果正確分開，支撐生成各自獨立
- [ ] 10.8 關閉 Gizmo 再重新開啟，所有狀態正確清除，無殘留 mesh 或錯誤索引
