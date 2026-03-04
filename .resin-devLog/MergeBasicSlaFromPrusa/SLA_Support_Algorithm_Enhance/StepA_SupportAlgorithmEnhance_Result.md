# Step A: SLA 支撐演算法升級 — 完成報告

**完成日期**: 2026-03-03
**狀態**: ✅ 完成（編譯 + 執行驗證通過）

---

## 任務目標

將 PrusaSlicer 2024 的新一代 SLA 支撐點生成演算法移植至 PhrozenOrca，
取代舊的 Poisson disk sampling，改用 **Voronoi Medial Axis + NearPoints KD-tree** 的
兩階段自由函數架構，使支撐點分布更均勻。

---

## 問題根因（移植前）

PhrozenOrca（Step 3.5 後）已在 island case 使用 `uniform_support_island()`，
但整體架構仍是舊的 **class-based SupportPointGenerator**（Poisson disk），缺少：

| 缺失功能 | 說明 |
|----------|------|
| `NearPoints` KD-tree | 跨層追蹤支撐點覆蓋範圍 |
| `prepare_generator_data()` | 預計算層間連接、懸空採樣、半島偵測 |
| `generate_support_points()` | 主生成迴圈（Voronoi + NearPoints） |
| `move_on_mesh_surface()` | 將平面採樣點投影到實際網格表面 |
| `SupportPointGeneratorData` | 預計算資料的持久化儲存 |

---

## 修改檔案總覽

| 步驟 | 檔案 | 修改內容 |
|------|------|----------|
| A1 | `SLA/SupportPointGenerator.hpp` | 完整重寫：移除舊 class；新增 LayerPart / LayerSupportPoint / SupportPointGeneratorData 等結構體與自由函數宣告 |
| A2 | `SLA/SupportPointGenerator.cpp` | 完整重寫：NearPoints KD-tree、prepare_generator_data()、generate_support_points()、move_on_mesh_surface() |
| A3 | `SLAPrint.hpp` | 新增 `m_support_point_generator_data` 成員至 SLAPrintObject |
| A4 | `SLAPrintSteps.hpp` | 宣告 `prepare_for_generate_supports()` |
| A5 | `SLAPrintSteps.cpp` | 實作 prepare_for_generate_supports()；重寫 support_points() 使用新 API |

---

## 詳細修改說明

### Step A1：SupportPointGenerator.hpp 重寫

移除：
- `class SupportPointGenerator`（class-based, Poisson disk）
- `struct Config`（舊設定結構）

新增結構體：
- `SupportPointGeneratorConfig` — 密度、head_diameter、support_curve
- `LayerPart` — 單一層 ExPolygon 片段，含 shape 指標、prev/next links、samples、peninsulas
- `LayerSupportPoint` — 擴充 SupportPoint，增加 position_on_layer、current_radius、is_permanent
- `Layer` / `Layers` — 按列分層結構
- `SupportPointGeneratorData` — 持久化預計算資料（slices + layers + permanent_supports）
- `Peninsula` / `Peninsulas` — 半島懸空偵測結構

新增自由函數宣告：
- `create_default_support_curve()`
- `create_default_island_configuration()`
- `prepare_generator_data()`
- `generate_support_points()`
- `move_on_mesh_surface()`
- `remove_bottom_points()`

### Step A2：SupportPointGenerator.cpp 重寫

**NearPoints KD-tree wrapper**（PhrozenOrca 特定適配）：
- 包裝 `KDTreeIndirect<2, coord_t, PointAccessor>`
- 新增 `std::vector<size_t> m_indices` 成員（因 PhrozenOrca 的 KDTreeIndirect 缺少 `get_nodes()` / `get_copy()`）
- 提供 `add()`、`remove_out_of()`、`get_copy()`、`merge()`、`exist_true_in_radius()` API

**prepare_generator_data()**：
1. 平行建立各層 LayerPart（shape 指標指向 slices）
2. 平行連接相鄰層 prev/next links（intersection 偵測重疊）
3. 移除不可支撐的微小模型片段（`get_small_parts`）
4. 平行採樣懸空輪廓線（`sample_overhangs`）
5. 平行偵測半島懸空（`create_peninsulas`）
6. 平行計算 extend_shape（用於支撐點失效判定）

**generate_support_points()**：
- 逐層處理：新 island → `support_island()`；繼續層 → `support_part_overhangs()` + `support_peninsulas()`
- 跨層傳播：prev_grids / grids 機制追蹤支撐覆蓋範圍
- 半徑曲線更新：`prepare_supports_for_layer()` 按高度差更新各點的影響半徑

**move_on_mesh_surface()**：
- 使用 `IndexedMesh`（PhrozenOrca 適配，API 與 PrusaSlicer AABBMesh 相同）
- 對每個點射線 up/down，投影到最近網格表面

### Step A3：SLAPrint.hpp

```cpp
// 新增於 SLAPrintObject private 成員（line 315）
sla::SupportPointGeneratorData m_support_point_generator_data;
```

### Step A4：SLAPrintSteps.hpp

```cpp
// 新增宣告（Steps class private section）
void prepare_for_generate_supports(SLAPrintObject &po);
```

### Step A5：SLAPrintSteps.cpp

**slice_model() 末段新增**：
```cpp
if (po.m_config.supports_enable.getBool())
    prepare_for_generate_supports(po);
```

**prepare_for_generate_supports() 實作**：
- 複製 `po.m_model_slices`（prepare_generator_data 會 move，原始資料需保留）
- 傳入 `po.m_model_height_levels` 作為高度陣列
- 結果存入 `po.m_support_point_generator_data`

**support_points() 重寫**：
- 建立 `SupportPointGeneratorConfig`（density、head_diameter）
- Phase 1：`generate_support_points()` → `LayerSupportPoints`
- Phase 2：`move_on_mesh_surface()` → `SupportPoints`（落在網格表面）

---

## Bug 修復（編譯 + 執行階段）

### Bug 1：C2397 narrowing conversion（SampleConfig.hpp）

**原因**：`scale_()` 回傳 `double`，直接賦值給 `float` 成員違反 MSVC C2397 規則。

**修復**（SampleConfig.hpp lines 24, 27, 33）：
```cpp
// 修復前
float peninsula_min_width = scale_(2);
// 修復後
float peninsula_min_width = float(scale_(2));
```

### Bug 2：C2039 — KDTreeIndirect 缺少 get_nodes() / get_copy()

**原因**：PrusaSlicer 的 `KDTreeIndirect` 有 `get_nodes()` / `get_copy()` API，
PhrozenOrca 版本沒有。

**修復**：NearPoints 新增 `std::vector<size_t> m_indices` 成員，自行追蹤作用中索引。

### Bug 3：KDTreeIndirect::build() 清空輸入向量（執行階段 — 支撐點連線問題）

**症狀**：自動支撐點全部連成直線，無法均勻分布。

**根本原因**：PhrozenOrca 的 `KDTreeIndirect::build(std::vector<size_t> &indices)` 在
建樹完成後會呼叫 `indices.clear()`。PrusaSlicer 版本則透過 `get_nodes()` 重新取得索引，
不依賴原始 vector 仍然有效。

**影響鏈**：
```
add(pt0):  m_indices=[0] → build(m_indices) → m_indices=[] (被清空!)
add(pt1):  m_indices=[1] → build(m_indices) → m_indices=[] (只有 pt1 在 tree)
add(pt2):  m_indices=[2] → build(m_indices) → m_indices=[] (只有 pt2 在 tree)
```
→ KD-tree 永遠只有最後一個點 → `exist_true_in_radius()` 幾乎永遠 false
→ `support_part_overhangs()` 對每個輪廓採樣都加點 → 沿邊緣的密集直線

同時影響：
- `remove_out_of()`：m_indices=[] → 不過濾任何點（no-op）
- `get_copy()`：複製出空的 m_indices → tree 為空
- `merge()`：合併兩個空 m_indices → 仍為空
- `prepare_supports_for_layer()`：get_indices() 回傳空 → 半徑曲線永不更新

**修復**（4 個 build 呼叫點）：
```cpp
// 修復前
m_tree.build(m_indices);  // m_indices 被清空

// 修復後
std::vector<size_t> tmp = m_indices;  // 先複製
m_tree.build(tmp);                    // 只有 tmp 被清空，m_indices 完好
```

---

## PhrozenOrca 適配摘要

| 差異點 | PrusaSlicer | PhrozenOrca 適配 |
|--------|-------------|-----------------|
| Mesh type | `AABBMesh` | `IndexedMesh`（API 相同） |
| KDTree | `get_nodes()` / `get_copy()` | 自行維護 `m_indices` |
| `build()` 行為 | 不清空輸入 | 清空輸入 → 傳 tmp copy |
| SupportPoint field | `is_new_island` (enum) | `is_new_island` (bool) |

---

## 驗證結果

- ✅ 編譯通過（無 C2397 / C2039 error）
- ✅ 執行測試通過：支撐點均勻分布，與 PrusaSlicer 一致
- ✅ 直線連續點問題已消除
- ✅ FDM 路徑不受影響（修改均在 SLA 相關路徑）
