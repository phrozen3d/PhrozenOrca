# SLA 切片流程說明

> 整理目的：說明 auto generate support points 與樹狀支撐顯示在完整切片流程中的位置，
> 以及各步驟實際做的事，作為 sla-island-visualization 實作的背景知識。

---

## 完整 Step 順序

```
slaposObjectSlice
    → slaposSupportPoints      ← auto generate support points
    → slaposSupportTree        ← 樹狀支撐顯示
    → slaposPad
    → slaposSliceSupports
    → slapsRasterize            ← 實際產生 2D 列印圖像
```

定義位置：`src/libslic3r/SLAPrint.hpp:26`（`SLAPrintObjectStep` enum）

---

## 各步驟說明

### slaposObjectSlice — `slice_model()`

- 將 3D mesh 切成每層的 ExPolygon
- 在結尾呼叫 `prepare_for_generate_supports()`，預先建立 `SupportPointGeneratorData`（含跨層連通性分析）
- 結果存入 `po.m_support_point_generator_data`

### slaposSupportPoints — `support_points()`

**只做演算法計算與資料結構建立，不產生 2D 圖像。**

1. 跑 Voronoi + NearPoints KD-tree 演算法（`generate_support_points()`）→ 決定每個點世界座標
2. `move_on_mesh_surface()` → 把點投影到實際 mesh 表面
3. 角度過濾（`support_critical_angle`）
4. 結果存入 `m_supportdata->pts`（`SupportPoints` vector）
5. 發送 `RELOAD_SLA_SUPPORT_POINTS` 通知 → GLGizmoSlaSupports 在 3D view 顯示支撐點球

實作位置：`src/libslic3r/SLAPrintSteps.cpp:662`

### slaposSupportTree — `support_tree()`

**只建立 3D Mesh 資料，不產生 2D 圖像。**

1. 以 `m_supportdata->pts` 為輸入，跑 `SupportTreeBuildsteps`（filterfn、create_ground_pillar 等）
2. 產出 `TriangleMesh`（支撐柱、橋、底座的 3D 幾何）
3. 發送 `RELOAD_SCENE` → 3D view 顯示白色半透明支撐結構

實作位置：`src/libslic3r/SLAPrintSteps.cpp:774`

### slaposSliceSupports

- 將 support_tree 產出的 3D mesh 切成每層的 ExPolygon
- 與模型切片合併成 `m_printer_input`（每層的 PrintLayer）

### slapsRasterize — `rasterize()`

**唯一實際產生 2D 列印圖像的步驟。**

1. 讀取 `m_printer_input` 中每層的 ExPolygon
2. 呼叫 `raster.draw(poly)` 畫入 raster buffer
3. 產出每層的 PNG/bitmap
4. 寫入輸出檔案（`.sl1` / `.zip`）

只在使用者執行「切片輸出」時才跑，在 Support Gizmo 的互動階段不會觸發。

實作位置：`src/libslic3r/SLAPrintSteps.cpp:1186`

---

## Island 判斷邏輯

Island 的判斷發生在 `slaposObjectSlice` 結尾的 `prepare_for_generate_supports()` 中，
以及 `slaposSupportPoints` 的 `generate_support_points()` 迴圈裡。

**核心條件：`LayerPart.prev_parts.empty() == true`**

流程：

1. 每層每個 ExPolygon 建立一個 `LayerPart`，初始 `prev_parts` 為空
2. 跨層幾何交集：上層 part 與下層有重疊 → `it_above->prev_parts.push_back(it_below)`
3. 支撐點生成時：
   ```cpp
   if (part.prev_parts.empty()) {
       // 這層 part 與任何下層 part 無幾何交集
       // → 第一次出現的懸空區塊 = island
       support_island(part, ...);
   }
   ```
4. `support_island()` 與 `support_peninsulas()` 建立的 `SupportPoint` 都標記 `SupportPointType::island`

相關位置：
- 跨層連結：`SupportPointGenerator.cpp:1111`
- island 判斷：`SupportPointGenerator.cpp:1205`
- island 型別標記：`SupportPointGenerator.cpp:267`

---

## 對 sla-island-visualization 的意義

- Island 輪廓（`part.shape`）在 `generate_support_points()` 內部即可提取，無需額外偵測
- 提取時機：`part.prev_parts.empty()` 為 true 時，`part.shape` 就是 island 的 ExPolygon
- 不需要 rasterize 就能取得島的幾何資料
- 視覺化只需在 `slaposSupportPoints` 完成後（`RELOAD_SLA_SUPPORT_POINTS`）將輪廓傳給 GLGizmo 渲染，不需要等到切片輸出
