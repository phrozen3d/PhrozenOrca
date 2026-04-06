## 背景

`SupportPointGenerator.cpp` 中的 `prepare_generator_data()` 已計算含 `prev_parts` 連結的 `LayerParts`。`prev_parts.empty()` 的部件就是 island，其 `shape`（一個 `ExPolygon*`）是該 Z 高度的 island 2D 輪廓。

目前此資料僅在 `generate_support_points()` 內部使用，之後即被丟棄。若要視覺化 island 區域，輪廓必須：
1. 在生成完成後從 `SupportPointGeneratorData.layers` 中提取
2. 提升至其 Z 座標形成 3D 多邊形
3. 在 `GLGizmoSlaSupports` 中渲染為模型表面的網格覆蓋層

`GLGizmoSlaBase` 中已透過 `GLVolumeCollection` 存在渲染模型。FDM 繪製 gizmo（`GLGizmoPainterBase`）展示了在模型表面繪製彩色覆蓋層的模式。

## 目標 / 非目標

**目標：**
- 將 island 區域輪廓渲染為模型表面上的彩色半透明多邊形
- 僅在 SLA 支撐 gizmo 啟用且已執行自動生成時顯示
- 透過 gizmo 面板中的核取方塊切換可見性
- 重新執行自動生成時更新輪廓

**非目標：**
- 每個 island 的互動功能（選取、點擊 island）——僅顯示
- 多尺度 island 偵測（更粗或更細的層高偵測）——未來功能
- 匯出 island 輪廓資料——初始實作不需要
- 顯示僅手動放置支撐組合的 island 輪廓

## 設計決策

### D1：Island 輪廓資料的儲存位置

將提取的 island 輪廓（以 `std::vector<std::pair<float, ExPolygons>>` 型別——Z 高度 + 該高度的多邊形）儲存在 `SLAPrintObject` 上，與支撐點並排。

**考慮過的替代方案**：直接儲存在 `GLGizmoSlaSupports` 中。已拒絕——gizmo 狀態是暫時性的（undo/redo 後會消失）；將輪廓綁定在 print object 上可與支撐點保持一致。

**考慮過的替代方案**：在 gizmo 中按需重新執行 `prepare_generator_data()`。已拒絕——成本高昂，且重複計算。

### D2：渲染方式

提取每個 Z 高度的 island ExPolygons，對其進行三角化（使用 Clipper/`triangulate_expolygon`），將 Z 稍微偏移至模型表面上方以避免 z-fighting，渲染為帶有半透明 island 顏色的 `GLVolume`（顏色與 `merge-support-point-type` 變更中的 island 點顏色一致）。

**考慮過的替代方案**：僅渲染輪廓線框。若對複雜模型的填充三角化速度過慢，此方案可作為備用。

### D3：輪廓計算時機

在 `generate_support_points()` 內部或呼叫返回前，作為後處理步驟計算 island 輪廓，與 `LayerSupportPoints` 結果一同儲存。

**考慮過的替代方案**：在呼叫返回後從 `SupportPointGeneratorData` 中提取。此為優選方案——將提取邏輯與生成邏輯分離。

### D4：輪廓顏色

使用與 `merge-support-point-type` 變更中 `SupportPointType::island` 點渲染相同的 `island_color`，填充區域使用約 40% alpha，輪廓線使用 100% alpha。

## 風險 / 取捨

- **效能**：含有許多小 island 分布在數百層的模型可能產生數千個多邊形。→ 實作最小面積閾值（例如，丟棄小於 `config.minimal_bounding_sphere_radius²π` 的 island）以控制多邊形數量。生成器中的 `get_small_parts()` 已有部分處理。
- **Z-fighting**：在與模型表面相同 Z 值渲染的 island 多邊形會閃爍。→ 將渲染多邊形在 Z 方向偏移 +0.01mm，或使用多邊形偏移。
- **對 merge-support-point-type 的依賴**：`island_color` 重用前一個變更中定義的顏色。→ 此變更應在 `merge-support-point-type` 完成後實作。

## 遷移計畫

不需變更檔案格式。Island 輪廓在執行時從模型幾何資料中計算，不持久化儲存。不需遷移。
