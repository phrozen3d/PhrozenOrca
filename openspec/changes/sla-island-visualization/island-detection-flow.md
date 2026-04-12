# Island 判斷流程說明

> 整理目的：詳細說明 SLA 切片流程中 island（孤島）的識別機制，
> 作為 sla-island-visualization 提取 island 輪廓資料的實作參考。

---

## 核心資料結構

### `LayerPart`（`SupportPointGenerator.hpp:77`）

每一層的每個 ExPolygon 輪廓對應一個 `LayerPart`：

```cpp
struct LayerPart {
    const ExPolygon *shape;       // 輪廓幾何（指向 slices 中的 ExPolygon）
    ExPolygons       extend_shape; // 擴張後的形狀（用於無效支撐點過濾）
    BoundingBox      shape_extent; // 軸對齊包圍盒（用於快速相交預判）
    Points           samples;      // 輪廓均勻取樣點
    PartLinks        prev_parts;   // 與此 part 有幾何交集的「下層」parts
    PartLinks        next_parts;   // 與此 part 有幾何交集的「上層」parts
    Peninsulas       peninsulas;   // 半島型懸空（部分無支撐的懸出區域）
};
```

**`prev_parts` 是 island 判斷的關鍵欄位。**

### `Peninsula`（`SupportPointGenerator.hpp:65`）

大面積懸出、僅一側有支撐的區域。與 island 不同，它有部分連接到下方幾何，但懸出側仍需支撐：

```cpp
struct Peninsula {
    ExPolygon           unsuported_area; // 無支撐的懸出區域
    std::vector<bool>   is_outline;      // 各邊線是否為懸出側（需支撐）
};
```

---

## 判斷流程（`prepare_generator_data()`）

流程發生在 `slaposObjectSlice` 步驟結尾的 `prepare_for_generate_supports()` 內。

### Step 1：建立 LayerPart（`SupportPointGenerator.cpp:1095`）

對每一層的每個 ExPolygon 建立一個 `LayerPart`，`prev_parts` 初始為空：

```cpp
for (const ExPolygon &island : result.slices[layer_id])
    layer.parts.push_back(LayerPart{
        &island, {},
        get_extents(island.contour)
    });
```

### Step 2：跨層連結（`SupportPointGenerator.cpp:1111`）

對相鄰兩層做幾何交集，建立上下層的連通關係：

```cpp
// 先用包圍盒快速排除不可能相交的組合
if (!it_above->shape_extent.overlap(it_below->shape_extent))
    continue;

// 精確計算幾何交集
Polygons polys = intersection(*it_above->shape, *it_below->shape);
if (polys.empty())
    continue;

// 有交集 → 建立雙向連結
it_above->prev_parts.push_back(it_below);  // 上層記錄「它有下方依托」
it_below->next_parts.push_back(it_above);  // 下層記錄「它有上方承載」
```

### Step 3：移除過小孤島（`SupportPointGenerator.cpp:1130`）

半徑小於 `minimal_bounding_sphere_radius` 的孤島在結構上無法有效支撐，直接從 layers 中移除：

```cpp
SmallParts small_parts = get_small_parts(result.layers, config.minimal_bounding_sphere_radius);
if (!small_parts.empty())
    erase(small_parts, result.layers);
```

### Step 4：偵測 Peninsula（`SupportPointGenerator.cpp:1149`）

針對 `prev_parts` 不為空（有下方依托）的 part，進一步判斷是否有懸出的「半島」區域：

```cpp
// 只處理有下層依托的 part
if (it_part->prev_parts.empty()) continue;
create_peninsulas(*it_part, config);
```

`create_peninsulas()` 邏輯：
1. 計算下層 shapes 的擴張輪廓（`peninsula_min_width`）
2. 用上層 shape 減去擴張結果 → 得到懸出太多的區域
3. 這些區域標記為 `Peninsula`，其邊線依是否為懸出側（coast）或連接側（land）分類

---

## Island 的最終判斷（`generate_support_points()`）

在 `slaposSupportPoints` 的 `generate_support_points()` 迴圈中，以 `prev_parts.empty()` 作為 island 判斷條件：

```cpp
for (const LayerPart &part : layer.parts) {
    if (part.prev_parts.empty()) {
        // prev_parts 為空 → 此 part 與下層任何輪廓無幾何交集
        // → 第一次出現的懸空區塊 = island
        support_island(part, ...);
        continue;
    }
    // prev_parts 不為空 → 有下方支撐，處理懸出邊 (peninsula) 和舊點傳播
    ...
}
```

### `support_island()`（`SupportPointGenerator.cpp:250`）

對確認為 island 的 part，使用 Voronoi Medial Axis 均勻取樣，建立 `SupportPoint`：

```cpp
SupportIslandPoints samples = uniform_support_island(*part.shape, permanent, cfg.island_configuration);
for (const SupportIslandPointPtr &sample : samples)
    near_points.add(LayerSupportPoint{
        SupportPoint{
            Vec3f{x, y, part_z},
            cfg.head_diameter / 2,
            SupportPointType::island    // ← 硬標記為 island
        }, ...
    });
```

### `support_peninsulas()`（`SupportPointGenerator.cpp:276`）

對 peninsula 區域（`part.prev_parts` 不為空，但有懸出邊），也標記為 `SupportPointType::island`：

```cpp
SupportPointType::island    // ← peninsula 的支撐點也使用 island 型別
```

---

## 三種 Part 類型對照

| 條件 | 類型 | 支撐點型別 | 說明 |
|------|------|-----------|------|
| `prev_parts.empty()` | **Island** | `SupportPointType::island` | 完全懸空，無下方依托 |
| `!prev_parts.empty()` 且有 `peninsulas` | **Peninsula** | `SupportPointType::island` | 部分懸空，懸出側需支撐 |
| `!prev_parts.empty()` 且無 `peninsulas` | **Slope/Overhang** | `SupportPointType::slope` | 一般懸出，從舊點傳播 |

---

## 對 sla-island-visualization 的意義

- **Island 輪廓的提取位置**：在 `generate_support_points()` 內 `part.prev_parts.empty()` 為 true 時，`part.shape` 就是該 island 的完整 ExPolygon
- **提取無需額外計算**：island 判斷在演算法流程中已完成，直接讀取 `part.shape` 即可
- **Peninsula 是否納入視覺化**：peninsula 支撐點也標記 `island`，但其 shape 為 `part.shape`（整個輪廓），視覺化時需考慮是否要用 `peninsula.unsuported_area` 代替
- **小孤島已被 `erase()` 移除**：進入 `generate_support_points()` 的 layers 已排除過小的 island，不需額外過濾
