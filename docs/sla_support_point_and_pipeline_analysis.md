# SLA 樹狀支撐：接觸點資料結構與 Pipeline 參數完整分析

## 前言

本文件分析 SLA 支撐點的資料結構與支撐樹生成 pipeline 中各參數的作用範圍，
目的是釐清哪些參數是**全域共用**、哪些已經是**逐點獨立**，
以及若要實現「每根支撐獨立設定柱子粗細、接觸面大小」需要補充什麼。

相關背景架構請參考：
- [sla_support_graph_architecture.md](sla_support_graph_architecture.md) — SupportGraph 新架構與渲染設計
- [sla_support_interactive_editing.md](sla_support_interactive_editing.md) — 三項互動編輯功能需求分析

---

## 階段一：只有接觸點時的資料結構

### SupportPoint struct

**相關檔案：** `src/libslic3r/SLA/SupportPoint.hpp:16`

```cpp
struct SupportPoint
{
    Vec3f pos;                 // 模型表面的 3D 位置
    float head_front_radius;   // 針尖半徑（唯一的逐點半徑欄位）
    bool  is_new_island;       // 是否為懸空孤立區域
};
```

**重要：** SupportPoint 只有 **一個** radius 欄位，而且是針尖（pin tip）的半徑，
不是柱子的粗細。柱子半徑在此階段尚未決定，由 pipeline 中的 config 決定。

---

### SupportableMesh — 接觸點進入 pipeline 的容器

**相關檔案：** `src/libslic3r/SLA/SupportTree.hpp:118`

```cpp
struct SupportableMesh {
    IndexedMesh       emesh;   // 模型的碰撞 mesh（用於 ray casting）
    SupportPoints     pts;     // std::vector<SupportPoint>
    SupportTreeConfig cfg;     // 全域參數（所有點共用同一份）
};
```

進入 pipeline 時，所有接觸點共用**同一份** `SupportTreeConfig`，
沒有 per-point 的柱子參數。

---

### GUI 管理的暫存結構

**相關檔案：** `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp:30`

```cpp
struct CacheEntry {
    sla::SupportPoint support_point;   // 含 pos 和 head_front_radius
    bool selected;
    Vec3f normal;                      // 接觸面的法向量（用於渲染和角度計算）
};
// GUI 中以 m_editing_cache (std::vector<CacheEntry>) 管理
```

---

### 全域參數：SupportTreeConfig

**相關檔案：** `src/libslic3r/SLA/SupportTree.hpp:32`

所有接觸點在此階段共用以下全域設定：

| 參數 | 預設值 | 對應 UI 設定 | 說明 |
|------|--------|-------------|------|
| `head_front_radius_mm` | 0.2 mm | `support_head_front_diameter / 2` | 針尖半徑（與 SupportPoint 同步） |
| `head_back_radius_mm` | 0.5 mm | `support_pillar_diameter / 2` | **柱子半徑（全域）** |
| `head_fallback_radius_mm` | 0.25 mm | `support_small_pillar_diameter_percent` | 空間不足時的縮小半徑 |
| `head_penetration_mm` | 0.5 mm | `support_head_penetration` | 針尖插入模型的深度 |
| `head_width_mm` | 1.0 mm | `support_head_width` | 針尖到球體的距離 |
| `base_radius_mm` | 2.0 mm | `support_base_diameter / 2` | 底座錐形半徑 |
| `base_height_mm` | 1.0 mm | `support_base_height` | 底座錐形高度 |
| `object_elevation_mm` | 10 mm | `support_object_elevation` | 模型距離 platform 的高度 |
| `max_bridge_length_mm` | 10.0 mm | `support_max_bridge_length` | bridge 最大長度 |
| `max_pillar_link_distance_mm` | 10.0 mm | `support_pillar_connection_mode` | pillar 互連最大距離 |
| `bridge_slope` | π/4 (45°) | `support_bridge_slope` | bridge 最小仰角 |
| `max_bridges_on_pillar` | 3 | `support_max_bridges_on_pillar` | 每根柱子最多連幾個 bridge |

**靜態常數（編譯期決定，無 UI）：**

| 常數 | 值 | 說明 |
|------|----|------|
| `normal_cutoff_angle` | 150° | 法向量切角，小於此角度才考慮支撐 |
| `safety_distance_mm` | 0.5 mm | 支撐與模型的最小安全距離 |
| `max_solo_pillar_height_mm` | 15.0 mm | 超過此高度需要至少 1 個鄰柱支撐 |
| `max_dual_pillar_height_mm` | 35.0 mm | 超過此高度需要至少 2 個鄰柱支撐 |
| `pillar_cascade_neighbors` | 3 | 每根柱子最多 cascade 連接幾個鄰居 |

---

### 階段一總結：逐點 vs 全域

| 資料 | 存放位置 | 逐點或全域 |
|------|----------|-----------|
| 位置 `pos` | `SupportPoint.pos` | 逐點 |
| 針尖半徑 `head_front_radius` | `SupportPoint.head_front_radius` | 逐點 |
| 法向量 `normal` | `CacheEntry.normal`（GUI 暫存） | 逐點（未寫入 SupportPoint） |
| 柱子半徑 | `SupportTreeConfig.head_back_radius_mm` | **全域** |
| 所有其他幾何參數 | `SupportTreeConfig` | **全域** |

---

## 階段二：從接觸點長出樹狀支撐 — Pipeline 各步驟參數分析

### 步驟 0：config 對應

**相關檔案：** `src/libslic3r/SLAPrint.cpp:37`

UI 設定轉換為 `SupportTreeConfig`：

```cpp
cfg.head_front_radius_mm  = 0.5 * config.support_head_front_diameter.getFloat();
cfg.head_back_radius_mm   = 0.5 * config.support_pillar_diameter.getFloat();
cfg.head_fallback_radius_mm = cfg.head_back_radius_mm
    * config.support_small_pillar_diameter_percent.getFloat() / 100.0;
// 其餘欄位直接映射
```

---

### 步驟 1：Filter — 逐點驗證、決定 Head 幾何

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuildsteps.cpp:629`

這是**唯一使用逐點 `head_front_radius` 的步驟**。

```cpp
// 每個 SupportPoint 建立一個 Head
Head head;
head.r_pin_mm       = sp.head_front_radius;          // ← 逐點，來自 SupportPoint
head.r_back_mm      = m_cfg.head_back_radius_mm;     // ← 全域初始值
head.penetration_mm = m_cfg.head_penetration_mm;     // ← 全域
head.width_mm       = m_cfg.head_width_mm;           // ← 全域
head.pos            = sp.pos.cast<double>();          // ← 逐點
```

碰撞測試（8 個 ray 從接觸點出發）：

```cpp
// 測試針尖空間夠不夠容納 head_back_radius
auto hit = pinhead_mesh_intersect(hp, nn,
    pin_r,    // head.r_pin_mm（逐點）
    back_r,   // head_back_radius_mm（全域，初始值）
    width);

// 如果空間不足，縮小 back_r 到 head_fallback_radius_mm 再試一次
if (碰撞) {
    if (back_r > m_cfg.head_fallback_radius_mm)
        filterfn(fidx, i, m_cfg.head_fallback_radius_mm);  // 遞迴，縮小半徑
    else
        head.invalidate();  // 完全無法放置，廢棄
}
```

**Filter 後的 Head 狀態：**

| 欄位 | 值的來源 | 逐點或全域 |
|------|----------|-----------|
| `r_pin_mm` | `SupportPoint.head_front_radius` | 逐點 |
| `r_back_mm` | `cfg.head_back_radius_mm` 或 `cfg.head_fallback_radius_mm` | 全域（但可能因空間不足而縮小） |
| `dir` | 模型法向量優化後的方向 | 逐點計算 |
| `pos` | `SupportPoint.pos` | 逐點 |
| `width_mm` | `cfg.head_width_mm` | 全域 |
| `penetration_mm` | `cfg.head_penetration_mm` | 全域 |

---

### 步驟 2：Classify — 分群與分類

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuildsteps.cpp:769`

```cpp
// 往 -Z 方向射線，判斷能否直達 ground
auto hit = bridge_mesh_intersect(
    head.junction_point(), DOWN, head.r_back_mm);  // 使用 Head 的 r_back_mm

if (std::isinf(hit.distance()))
    m_iheads.push_back(i);          // ground-facing
else
    m_iheads_onmodel.push_back(i);  // model-facing，需要 bridge 繞路
```

分群（clustering）：以 `2 × cfg.base_radius_mm` 為距離門檻，
XY 平面距離近的 ground-facing head 歸為同一 cluster，共用一根中心柱。

---

### 步驟 3：Routing to Ground — 柱子的建立

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuildsteps.cpp:818`

**柱子半徑的決定流程（最重要的部分）：**

```
head.r_back_mm（在 filter 步驟設定）
    ↓
create_ground_pillar(head.junction_point(), head.dir, head.r_back_mm, head.id)
    ↓
m_builder.add_pillar(head_id, height)
    ↓ （SupportTreeBuilder.hpp:273）
Pillar.r = head.r_back_mm    ← 柱子半徑 = head 的 back radius
```

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuilder.hpp:283`

```cpp
// add_pillar 從 head 建立時，直接繼承 head.r_back_mm
m_pillars.emplace_back(hjp, length, head.r_back_mm);
```

**Cluster 內的 side head 處理：**

```cpp
// 優先嘗試 bridge 連到 cluster 的中心柱
if (!connect_to_nearpillar(sidehead, centerpillar_id)) {
    // 失敗時建立獨立柱，使用自己的 r_back_mm
    create_ground_pillar(pstart, sidehead.dir, sidehead.r_back_mm, sidehead.id);
}
```

每個 cluster 的中心柱半徑來自中心 Head 的 `r_back_mm`，
side head 連到中心柱的 bridge 半徑：

```cpp
// connect_to_nearpillar 中
m_builder.add_bridge(head.id, endp);
// bridge.r = head.r_back_mm（SupportTreeBuilder.cpp:364）
```

---

### 步驟 4：create_ground_pillar 內部邏輯

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuildsteps.cpp:468`

決定柱子是否需要「加寬」（DiffBridge）：

```cpp
bool create_ground_pillar(const Vec3d &jp, const Vec3d &dir, double radius, long head_id)
{
    // 判斷是否為 mini pillar（半徑 < head_back_radius_mm）
    bool is_mini = radius < m_cfg.head_back_radius_mm - EPSILON;

    // mini pillar 且高度過高時，用 DiffBridge 從細過渡到粗
    if (is_mini && pillar_height > 20 * radius) {
        auto wpath = search_widening_path(jp, dir, radius, m_cfg.head_back_radius_mm);
        if (wpath) {
            m_builder.add_diffbridge(*wpath);   // 錐形過渡段
            // 之後接標準粗細的 pillar
        }
    }

    // 建立 Pillar，高度由落點到 ground 決定
    // 半徑 = 傳入的 radius（= head.r_back_mm）
    long pid = m_builder.add_pillar(head_id, height);
    add_pillar_base(pid);   // 底座（cone），半徑 = cfg.base_radius_mm（全域）
}
```

底座（Pedestal）的半徑固定來自 `cfg.base_radius_mm`，不隨柱子半徑變化。

---

### 步驟 5：Routing to Model — 非落地接觸點的處理

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuildsteps.cpp:1034`

三種路由方式各自的半徑使用：

| 路由方式 | 使用的半徑 | 說明 |
|----------|-----------|------|
| `search_pillar_and_connect()` → bridge 連既有柱 | `head.r_back_mm` | bridge 半徑來自 head |
| `connect_to_ground()` → 斜向落地 | `head.r_back_mm` | pillar 半徑來自 head |
| `connect_to_model_body()` → Anchor 接模型 | `head.r_back_mm` / `head.r_pin_mm` | Anchor 兩端各自的半徑 |

`connect_to_nearpillar` 中的 bridge 半徑：

```cpp
// SupportTreeBuildsteps.cpp:250
m_builder.add_bridge(source_head.id, endp);
// 此時 bridge.r 由 add_bridge(headid, endp) 設定
// → bridge.r = head.r_back_mm（SupportTreeBuilder.cpp:364）
```

---

### 步驟 6：Interconnect Pillars — 柱子互連

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuildsteps.cpp:1064`

**何時觸發互連：**

```
pillar 高度 > max_solo_pillar_height_mm (15 mm)  → 需要至少 1 個鄰柱
pillar 高度 > max_dual_pillar_height_mm (35 mm)  → 需要至少 2 個鄰柱
```

**crossbridge 的半徑：**

```cpp
// interconnect() 函數中
// SupportTreeBuildsteps.cpp:361
m_builder.add_crossbridge(start_junction, end_junction, pillar.r);
//                                                      ↑ 使用來源柱子的半徑
```

**Cascade 新增支撐柱：**

當高柱子找不到足夠的鄰居時，會在兩柱之間插入新柱：

```cpp
// 新柱的半徑繼承自原柱
Pillar new_p(ground_point, height, pillar.r);   // pillar.r 傳遞
```

**距離門檻的計算（與半徑有關）：**

```cpp
// SupportTreeBuildsteps.cpp:1100
double max_d = cfg.max_pillar_link_distance_mm
             * pillar.r / cfg.head_back_radius_mm;
// 細柱子的有效連接距離比粗柱子短（等比縮放）
```

---

## 完整參數作用範圍總表

| 參數 | 作用範圍 | 影響的幾何 | 在哪裡使用 |
|------|----------|-----------|-----------|
| `SupportPoint.head_front_radius` | **逐點** | Head 針尖球半徑 | filter() line 700 |
| `SupportPoint.pos` | **逐點** | 接觸位置 | filter() line 661 |
| `cfg.head_back_radius_mm` | **全域初始** | Head back sphere → Pillar 半徑 → Bridge 半徑 | filter() line 753 |
| `cfg.head_fallback_radius_mm` | **全域，fallback** | 空間不足時的較小 Head/Pillar 半徑 | filter() line 747 |
| `cfg.head_penetration_mm` | 全域 | 針尖插入深度 | filter() line 662 |
| `cfg.head_width_mm` | 全域 | 針尖到球體的距離 | filter() line 663 |
| `cfg.base_radius_mm` | 全域 | 底座（Pedestal）半徑 | add_pillar_base() |
| `cfg.base_height_mm` | 全域 | 底座（Pedestal）高度 | add_pillar_base() |
| `cfg.max_bridge_length_mm` | 全域 | bridge 最大允許長度 | connect_to_nearpillar() |
| `cfg.bridge_slope` | 全域 | bridge 最小仰角 | 所有 bridge 路由 |
| `cfg.max_bridges_on_pillar` | 全域 | 每根柱子最多掛幾個 bridge | connect_to_nearpillar() |
| `cfg.max_pillar_link_distance_mm` | 全域（但按半徑縮放） | pillar 互連最大距離 | interconnect_pillars() |
| `max_solo_pillar_height_mm` | 靜態常數 | 觸發 1 鄰柱需求的高度 | interconnect_pillars() |
| `max_dual_pillar_height_mm` | 靜態常數 | 觸發 2 鄰柱需求的高度 | interconnect_pillars() |
| `pillar_cascade_neighbors` | 靜態常數 | cascade 最多幾個鄰居 | interconnect_pillars() |

---

## 柱子粗細的完整決定鏈

```
UI 設定 support_pillar_diameter
    ↓
SLAPrint.cpp:cfg.head_back_radius_mm = diameter / 2
    ↓
SupportTreeBuildsteps::filter()
    ├─ 正常情況：head.r_back_mm = cfg.head_back_radius_mm  （全域值）
    └─ 空間不足：head.r_back_mm = cfg.head_fallback_radius_mm （全域縮小值）
    ↓
SupportTreeBuilder::add_pillar(head_id, height)
    → Pillar.r = head.r_back_mm                             （繼承 Head 的值）
    ↓
interconnect() 建立 crossbridge
    → crossbridge.r = pillar.r                              （繼承 Pillar 的值）
    ↓
cascade 新增柱子
    → new_pillar.r = existing_pillar.r                      （繼承既有柱子的值）
```

結論：**所有柱子、bridge、crossbridge 的半徑，最終都來自同一個全域參數 `head_back_radius_mm`**，
差別只有是否被 fallback 縮小過。

---

## 若要實現逐點獨立柱子半徑，需要補充的部分

### 需要新增的欄位

**`src/libslic3r/SLA/SupportPoint.hpp`：**

```cpp
struct SupportPoint {
    Vec3f pos;
    float head_front_radius;
    float head_back_radius;   // 新增：逐點柱子半徑（0 表示使用 config 預設）
    bool  is_new_island;
};
```

### 需要更動的位置

| 檔案 | 更動位置 | 更動內容 |
|------|----------|----------|
| `src/libslic3r/SLA/SupportPoint.hpp` | struct 定義 | 新增 `head_back_radius` 欄位 |
| `src/libslic3r/SLA/SupportTreeBuildsteps.cpp` | `filter()` line 753 | 若 `sp.head_back_radius > 0` 則用逐點值，否則用 `cfg.head_back_radius_mm` |
| `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` | `CacheEntry` | 補充欄位讓 GUI 可編輯 |
| `src/libslic3r/Format/3mf.cpp` | 序列化 | 儲存/載入新欄位 |
| `src/libslic3r/SLA/SupportPointGenerator.cpp` | 自動生成 | 自動生成時用 config 預設填入 |

**更動量極小**，`SupportTreeBuildsteps` 的演算法邏輯完全不需要改，
只在 `filter()` 的一行改為優先讀 per-point 值。
