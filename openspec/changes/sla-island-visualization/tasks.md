## 1. 資料提取

- [ ] 1.1 在 `SupportPointGenerator.hpp` 中：新增 `IslandContours` 型別別名（`std::vector<std::pair<float, ExPolygons>>`）並在 `SupportPointGeneratorData` 中新增對應欄位
- [ ] 1.2 在 `SupportPointGenerator.cpp` 中：在 `generate_support_points()` 的層迴圈後提取 island 輪廓——對每一層，收集所有 `prev_parts.empty()` 部件的 `*part.shape`，與 `layer.print_z` 一同儲存
- [ ] 1.3 套用最小面積篩選：跳過小於 `config.minimal_bounding_sphere_radius² * M_PI` 的 island
- [ ] 1.4 將提取的輪廓儲存至 `SupportPointGeneratorData.island_contours`

## 2. 資料管線到 Gizmo

- [ ] 2.1 在 `SLAPrint.hpp` 中：在 `SLAPrintObject` 新增 `m_island_contours` 成員（`IslandContours`）
- [ ] 2.2 在 `SLAPrintSteps.cpp` 支撐點步驟中：呼叫 `generate_support_points()` 後，將 `data.island_contours` 複製至 `print_object.m_island_contours`
- [ ] 2.3 在 `GLGizmoSlaSupports` 中：新增 `get_island_contours()` 存取子呼叫，從 `SLAPrintObject` 取得輪廓（類似透過 `get_data_from_backend()` 取得支撐點的方式）
- [ ] 2.4 當支撐 gizmo 重新載入或自動生成結果失效時，清除 `m_island_contour_mesh`

## 3. 網格生成

- [ ] 3.1 在 `GLGizmoSlaSupports` 中新增 `m_island_contour_mesh`（GLModel 或 indexed_triangle_set）和 `m_show_island_contours` bool
- [ ] 3.2 實作 `build_island_contour_mesh()`：迭代輪廓，使用 `triangulate_expolygon_2f()` 對每個 ExPolygon 進行三角化，將頂點提升至 Z + 0.01mm 偏移，附加至網格
- [ ] 3.3 為輪廓網格體積指派約 40% alpha 的 island 顏色

## 4. 渲染

- [ ] 4.1 在 `GLGizmoSlaSupports::on_render()` 中：若 `m_show_island_contours && !m_island_contour_mesh.empty()`，啟用混色渲染輪廓網格
- [ ] 4.2 確保輪廓網格僅在 gizmo 啟用時渲染（由 `on_render()` 作用域自然保證）

## 5. UI 面板

- [ ] 5.1 在 `on_render_input_window()` 中：在非編輯模式面板區塊新增 `ImGuiPureWrap::checkbox("Show island regions", m_show_island_contours)`
- [ ] 5.2 首次啟用核取方塊時重建輪廓網格（延遲建構）

## 6. 驗證

- [ ] 6.1 編譯無錯誤
- [ ] 6.2 開啟含明確 island 幾何形狀的模型（例如含水平突出部的模型），執行自動生成，確認 island 區域上出現彩色覆蓋層
- [ ] 6.3 切換核取方塊關閉/開啟——覆蓋層正確隱藏和顯示
- [ ] 6.4 以不同密度重新執行自動生成——確認覆蓋層更新
- [ ] 6.5 確認選擇 FDM 印表機時無覆蓋層顯示
- [ ] 6.6 確認 gizmo 關閉時無覆蓋層顯示
