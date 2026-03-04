# Step 3.2：BranchingTree 函式庫移植

## 完成狀態：✅ 完成（編譯 + 執行驗證通過）

---

## 修改摘要

從 PrusaSlicer 複製 `BranchingTree/` 目錄（4 個檔案）至 PhrozenOrca，並更新 CMakeLists.txt。

### 依賴分析

| 依賴 | 狀態 | 說明 |
|------|:----:|------|
| `libigl` | ✅ 已鏈結 | `libslic3r` PUBLIC target 中已有 `libigl`（line 567） |
| `libslic3r/SLA/SupportTreeUtils.hpp` | ❌ PhrozenOrca 無此檔 | `find_merge_pt` 改為 inline 實作（見下方說明） |
| `boost::geometry` | ✅ 已鏈結 | boost_libs 在 PRIVATE target |
| `libslic3r/MutablePriorityQueue.hpp` | ✅ 存在 | PhrozenOrca 有此檔 |
| `libslic3r/BoostAdapter.hpp` | ✅ 存在 | PhrozenOrca 有此檔 |

### 關鍵修改：find_merge_pt inline 化

**原因**：`PointCloud.cpp` 引用 `sla::find_merge_pt()` via `SupportTreeUtils.hpp`，但 PhrozenOrca 無此檔（949 行，含 NLopt/Execution.hpp 等重度依賴）。

**解決方案**：將 `find_merge_pt` 函數直接 inline 在 `PointCloud.cpp` 的 `branchingtree` namespace 中：
- 完全 identical to PrusaSlicer's `sla::find_merge_pt` implementation
- 函數為純 Eigen 幾何計算，無外部依賴
- 去掉 `#include "libslic3r/SLA/SupportTreeUtils.hpp"`，改為直接定義

```cpp
// PointCloud.cpp (branchingtree namespace)
std::optional<Vec3f> find_merge_pt(const Vec3f &A, const Vec3f &B, float critical_angle)
{
    // 2D projection to find Y-shaped merger point between two support branches
    // ... [identical to SupportTreeUtils.hpp sla::find_merge_pt, line 875]
}
```

---

## 新增 / 修改的檔案

### 1. 新增：BranchingTree/BranchingTree.hpp（165 行）

**路徑**：`PhrozenOrca/src/libslic3r/BranchingTree/BranchingTree.hpp`

- `Properties` class：max_slope / ground_level / sampling_radius / bed_shape / max_branch_length
- `Node` struct：pos, Rmin, weight, id, left, right
- `Builder` abstract class：add_bridge / add_merger / add_ground_bridge / add_mesh_bridge / report_unroutable
- `build_tree()` free function（兩個多載）
- `make_bed_poly()` helper

**100% identical to PrusaSlicer**（純 Eigen + std，無特殊依賴）

---

### 2. 新增：BranchingTree/BranchingTree.cpp（203 行）

**路徑**：`PhrozenOrca/src/libslic3r/BranchingTree/BranchingTree.cpp`

- `build_tree(PointCloud &, Builder &)` — 核心演算法（Prim's algorithm 變體）
- `build_tree(const indexed_triangle_set &, ...)` — 外部 API，建立 PointCloud 後呼叫上者
- `make_bed_poly()` — bounding box 轉 ExPolygon

**100% identical to PrusaSlicer**

---

### 3. 新增：BranchingTree/PointCloud.hpp（316 行）

**路徑**：`PhrozenOrca/src/libslic3r/BranchingTree/PointCloud.hpp`

- `find_merge_pt()` 宣告（實作在 PointCloud.cpp）
- `to_eigen_mesh()`, `sample_mesh()`, `sample_bed()` 工具函數宣告
- `PtType` enum：LEAF / MESH / BED / JUNCTION / NONE
- `PointCloud` class：支撐點雲管理（R-tree 空間索引）
- `traverse()` template helper

**100% identical to PrusaSlicer**（原本 commented-out 的 `//Execution.hpp` include 保留）

---

### 4. 新增：BranchingTree/PointCloud.cpp（修改版）

**路徑**：`PhrozenOrca/src/libslic3r/BranchingTree/PointCloud.cpp`

與 PrusaSlicer 版本的差異：

| 項目 | PrusaSlicer | PhrozenOrca |
|------|-------------|-------------|
| `#include "libslic3r/SLA/SupportTreeUtils.hpp"` | ✅ 有 | ❌ 移除（檔案不存在） |
| `find_merge_pt` 實作 | `return sla::find_merge_pt(A, B, max_slope)` | 直接 inline 幾何實作 |

其餘（`igl::random_points_on_mesh`, `sample_mesh`, `sample_bed`, `PointCloud` 建構子, `get_distance`）**100% identical**。

---

### 5. 修改：CMakeLists.txt（+6 行）

**位置**：SLA 區塊末尾（`SLA/ZCorrection.cpp` 之後），`Arachne/...` 之前

```cmake
SLA/SupportTreeStrategies.hpp      # Step 3.1 補入（IDE 可見性）
BranchingTree/BranchingTree.hpp
BranchingTree/BranchingTree.cpp
BranchingTree/PointCloud.hpp
BranchingTree/PointCloud.cpp
```

---

## 架構說明

```
BranchingTree/ 函式庫（Step 3.2）
│
├── BranchingTree.hpp/cpp
│   ├── Properties          — 輸入參數（slope, radius, bed shape...）
│   ├── Node                — 樹節點（pos, weight, Rmin）
│   ├── Builder             — 輸出介面（abstract）
│   └── build_tree()        — Prim's algorithm 變體：從支撐葉節點向上合併
│
└── PointCloud.hpp/cpp
    ├── PointCloud          — 4 種點的空間索引（R-tree）
    ├── find_merge_pt()     — 幾何：兩個支撐錐的交點
    ├── sample_mesh()       — igl::random_points_on_mesh 採樣
    └── sample_bed()        — 底板三角化採樣
        ↓
    Step 3.3：BranchingTreeSLA.hpp/cpp 將 include BranchingTree.hpp
              並實作 Builder 介面
```

---

## FDM 影響

**零影響**：
- `BranchingTree/` 是純新增目錄
- 沒有任何現有 FDM 路徑 include 這些檔案
- `PointCloud.cpp` 的 `igl::random_points_on_mesh` 已在 `libslic3r` PUBLIC target 中

---

## 修改清單

| 檔案 | 操作 | 說明 |
|------|:----:|------|
| `PhrozenOrca/src/libslic3r/BranchingTree/BranchingTree.hpp` | 新增 | 與 PrusaSlicer 100% identical |
| `PhrozenOrca/src/libslic3r/BranchingTree/BranchingTree.cpp` | 新增 | 與 PrusaSlicer 100% identical |
| `PhrozenOrca/src/libslic3r/BranchingTree/PointCloud.hpp` | 新增 | 與 PrusaSlicer 100% identical |
| `PhrozenOrca/src/libslic3r/BranchingTree/PointCloud.cpp` | 新增 | 移除 SupportTreeUtils.hpp，find_merge_pt inline 化 |
| `PhrozenOrca/src/libslic3r/CMakeLists.txt` | 修改 | 加入 5 個新項目（含 Step 3.1 補充的 SupportTreeStrategies.hpp） |

---

## 下一步

- **Step 3.3**：移植 `BranchingTreeSLA.hpp/cpp`，實作 `Builder` 介面
  - 依賴：`BranchingTree/BranchingTree.hpp`（Step 3.2 完成後可進行）
  - 主要工作：IndexedMesh → AABBMesh API 差異處理
  - 完成後：`SupportTree.cpp` 中的 fallback 替換為 `create_branching_tree(*builder, sm)`
