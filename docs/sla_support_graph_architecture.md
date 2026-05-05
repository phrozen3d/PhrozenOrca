# SLA 支撐樹架構比較：現有 Pipeline vs 互動式 SupportGraph

## 背景

本文件比較 PhrozenOrca 現有的 SLA 樹狀支撐生成機制，與仿照 Chitubox 互動式編輯設計的 SupportGraph 架構之間的差異，並列出實作新架構所需更動的範圍。

---

## 一、資料模型

### 現況：分類型平行 vectors，弱連結

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuilder.hpp:220`

```cpp
class SupportTreeBuilder : public SupportTree {
    std::vector<Head>       m_heads;
    std::vector<Pillar>     m_pillars;
    std::vector<Junction>   m_junctions;
    std::vector<Bridge>     m_bridges;
    std::vector<Bridge>     m_crossbridges;
    std::vector<DiffBridge> m_diffbridges;
    std::vector<Pedestal>   m_pedestals;
    std::vector<Anchor>     m_anchors;
};
```

各型別獨立儲存，節點間只有單向、不完整的連結：

```cpp
Head.pillar_id           // 此 Head 連到哪個 Pillar（單向）
Head.bridge_id           // 此 Head 連到哪個 Bridge（單向）
Pillar.start_junction_id // 從哪個 Head/Junction 出發（單向）
Pillar.bridges           // 掛載的 bridge 數量（計數，無 id list）
```

沒有反向查詢能力：無法從 Pillar 找出所有連接的 Bridge，也無法從 Bridge 找出兩端節點的完整資訊。

---

### 新架構：統一圖節點，明確雙向連結

```cpp
enum class SupportNodeType { Tip, Branch, Root };

struct SupportGraphNode {
    int     id;
    Vec3d   position;
    float   radius;
    SupportNodeType type;

    int              parent_id    = -1;  // 往 platform 方向（唯一父節點）
    std::vector<int> children_ids;       // 往 model 方向（可多個子節點）

    // per-node mesh cache
    indexed_triangle_set node_mesh;      // 自身幾何（sphere / junction）
    indexed_triangle_set edge_mesh;      // 往 parent 的 cylinder / cone
    bool mesh_dirty = true;
    bool edge_dirty = true;
};

struct SupportGraph {
    std::vector<SupportGraphNode>        nodes;
    std::unordered_map<int, int>         support_point_to_node; // SupportPoint id → node id
};
```

每個節點完整記錄父節點與所有子節點，支援雙向遍歷，可做局部更新。

---

## 二、Mesh Cache 機制

### 現況：全域 single dirty flag

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuilder.hpp:238`、`SupportTreeBuilder.cpp:125`

```cpp
mutable indexed_triangle_set m_meshcache;
mutable bool m_meshcache_valid = false;
```

任何一個節點新增或修改 → `m_meshcache_valid = false` → 下次查詢時整棵樹全部重算：

```cpp
// merged_mesh()：無局部更新，每次全量 rebuild
for (auto &head : m_heads)   its_merge(merged, get_mesh(head, steps));
for (auto &pill : m_pillars) its_merge(merged, get_mesh(pill, steps));
for (auto &j    : m_junctions) its_merge(merged, get_mesh(j, steps));
// ...以此類推
```

**結果：** 移動任一支撐點 → 整棵樹重算 → 大量無謂的 CPU 與 GPU 工作。

---

### 新架構：per-node dirty flag，局部重算

```cpp
// 移動 node N → 只標記 N 及直接相鄰的 edge 為 dirty
void mark_dirty(int node_id) {
    nodes[node_id].mesh_dirty = true;
    if (nodes[node_id].parent_id >= 0)
        nodes[nodes[node_id].parent_id].edge_dirty = true;
    for (int child_id : nodes[node_id].children_ids)
        nodes[child_id].edge_dirty = true;
}

// rebuild 時只處理 dirty 的部分
void rebuild_dirty_meshes() {
    for (auto &node : nodes) {
        if (node.mesh_dirty) {
            node.node_mesh = compute_node_mesh(node);
            node.mesh_dirty = false;
        }
        if (node.edge_dirty && node.parent_id >= 0) {
            node.edge_mesh = compute_edge_mesh(node, nodes[node.parent_id]);
            node.edge_dirty = false;
        }
    }
}
```

每次更新的計算量 = O(degree(N))，與樹的總大小無關。

---

## 三、演算法流程

### 現況：7 步驟線性 Pipeline，強全域依賴

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuildsteps.hpp`、`SupportTreeBuildsteps.cpp`

```
SupportPoints
    │
    ▼ FILTER ─────────────────── 逐點獨立（可平行）
    │  生成 Head，過濾過近的支撐點
    │
    ▼ CLASSIFY ───────────────── 全域 clustering（關鍵耦合點）
    │  判定 ground-facing vs model-facing
    │  計算 m_pillar_clusters：所有 ground head 的 XY 距離分群
    │  → 必須看到所有 Head 才能分群
    │
    ▼ ROUTING_GROUND ─────────── 以 cluster 為單位
    │  每個 cluster 找幾何重心 → 建中心 Pillar
    │  其他 side-head → Bridge 連到中心 Pillar
    │  → 一個 cluster 的 side-head routing 依賴中心 Pillar 的位置
    │
    ▼ ROUTING_NONGROUND ──────── 依賴 m_pillar_index（全部已建 Pillar）
    │  找最近 Pillar → Bridge 連接
    │  找不到 → connect_to_ground（NLopt 數值最佳化）
    │  全失敗 → Anchor 接到 model body
    │
    ▼ CASCADE_PILLARS ────────── 全樹依賴
    │  高度 > 15mm → 需要至少 1 個鄰居 Pillar 支撐
    │  高度 > 35mm → 需要至少 2 個鄰居 Pillar 支撐
    │  → 必須看到所有 Pillar 才能決定是否追加
    │
    ▼ MERGE_RESULT
       m_meshcache = 所有幾何合併成一個 indexed_triangle_set
```

**關鍵限制：** 任何 node 的位置改變，都必須從 CLASSIFY 重跑，因為 `m_pillar_clusters` 與 `m_pillar_index` 都是全域狀態，無法局部更新。

---

### 新架構：初次建樹 + 局部互動更新兩種模式

```
[初次建立] 自動演算法填入 SupportGraph
    │
    ├─ 方案 A：保留現有 pipeline，結束後轉換
    │   SupportTreeBuildsteps::execute() → 跑完整 pipeline
    │   → to_graph()：將 Head/Pillar/Bridge 轉換為 SupportGraphNode
    │      建立 parent_id / children_ids 雙向連結
    │
    └─ 方案 B：改用 bottom-up barycenter 演算法直接建圖
        overhang 區域 → 子區域分割
        → 每個子區域往下合併到局部重心
        → 形成自然的樹狀拓撲，直接建成 SupportGraph

[使用者互動編輯]
    │
    ├─ 移動 Node N 的接觸位置（touch point）
    │   → 更新 node.position
    │   → mark_dirty(N)
    │   → rebuild_dirty_meshes()  ← 只重算 O(degree) 個 mesh
    │   → 更新對應的 GLModel       ← 只重傳 dirty 的 GPU buffer
    │
    ├─ 新增支撐點
    │   → 建立 Tip node（type = Tip）
    │   → 往下找最近的 Branch / Root node
    │   → 插入 SupportGraph，設定 parent_id / children_ids
    │   → mark_dirty(new_node)
    │
    └─ 刪除 Node N
        → 若 N 有 children → 將 children re-attach 到 N 的 parent
        → 從 SupportGraph 移除 N
        → mark_dirty(affected nodes)
```

---

## 四、渲染層

### 現況：整棵樹合併為一個 GLVolume

**相關檔案：** `src/slic3r/GUI/3DScene.cpp:748`

```cpp
void GLVolumeCollection::load_object_auxiliary(
    const SLAPrintObject *print_object, ..., SLAPrintObjectStep milestone, ...)
{
    TriangleMesh mesh = print_object->get_mesh(milestone); // 整棵樹合併的 mesh
    GLVolume &v = *this->volumes.emplace_back(new GLVolume(SLA_SUPPORT_COLOR));
    v.model.init_from(mesh); // 一次上傳所有三角面到 GPU
    v.shader_outside_printer_detection_enabled = (milestone == slaposSupportTree);
}
```

**限制：**
- GPU 上只有一個 draw call，無法 hit-test 個別支撐
- 無法 highlight 或選取單一支撐分支
- 任何修改都需重傳整個 mesh buffer

---

### 新架構：每個 node 對應獨立 GLModel

```
SupportGraph
    ├─ node[0]: node_mesh → GLModel[0]  (sphere/junction 幾何)
    │           edge_mesh → GLModel[1]  (往 parent 的 cylinder/cone)
    ├─ node[1]: node_mesh → GLModel[2]
    │           edge_mesh → GLModel[3]
    │ ...
    └─ 每個 GLModel 對應一個 GPU draw call
```

優點：
- Raycast 命中特定 GLModel → 對應到具體的 node id
- 選取後可獨立 highlight 單一支撐
- 移動後只 `init_from(node.node_mesh)` 重傳一個 node 的 VBO，其餘不動

---

## 五、單一支撐點更新的計算範圍比較

| 操作 | 現有架構 | SupportGraph |
|------|----------|--------------|
| 移動一個孤立支撐點 | 重跑整個 pipeline + 全量 merge_mesh | 重算 1 個 node_mesh + 1 個 edge_mesh |
| 移動一個 cluster 成員 | 重跑整個 pipeline | 重算該 node + 所有相鄰 edge（O(degree)）|
| 移動一個 cluster 中心 | 重跑整個 pipeline（cluster 重心改變，影響所有 side-head） | 重算中心 node + 所有 children edge |
| 刪除一個支撐點 | 重跑整個 pipeline | 重算受影響的 parent + children |
| 新增一個支撐點 | 重跑整個 pipeline | 建立新 node，重算新 node 及 parent edge |

---

## 六、需要更動的範圍

### 新建檔案

| 檔案 | 用途 |
|------|------|
| `src/libslic3r/SLA/SupportGraph.hpp` | `SupportGraphNode`、`SupportGraph` 資料結構定義 |
| `src/libslic3r/SLA/SupportGraph.cpp` | `mark_dirty()`、`rebuild_dirty_meshes()`、`to_graph()` 轉換函數 |
| `src/slic3r/GUI/Gizmos/GLGizmoSLASupportEdit.hpp/.cpp` | 支撐互動編輯 Gizmo（選取、拖曳、新增、刪除） |

### 修改現有檔案

| 檔案 | 更動內容 |
|------|----------|
| `src/libslic3r/SLA/SupportTree.hpp` | 新增虛函數 `retrieve_graph() const` |
| `src/libslic3r/SLA/SupportTreeBuilder.hpp/.cpp` | 新增 `to_graph()` 實作，將現有 nodes 轉換為 `SupportGraph` |
| `src/libslic3r/SLAPrint.hpp` | `SupportData` 加入 `SupportGraph graph_data` 欄位 |
| `src/libslic3r/SLAPrint.cpp` | pipeline 完成後呼叫 `to_graph()` 填入 `graph_data` |
| `src/slic3r/GUI/3DScene.hpp` | `GLVolume` 加入 `support_node_id` 欄位，用於 hit-test 識別 |
| `src/slic3r/GUI/3DScene.cpp` | 新增 `load_object_support_graph()` 函數，取代 `load_object_auxiliary()` 的 SLA 支撐路徑 |
| `src/slic3r/GUI/GLCanvas3D.cpp` | Prepare tab 的支撐載入改呼叫 `load_object_support_graph()` |
| `src/slic3r/GUI/Gizmos/GLGizmosManager.cpp` | 註冊新的 SLA support edit gizmo |

### 不需更動

| 檔案 | 原因 |
|------|------|
| `src/libslic3r/SLA/SupportTreeBuildsteps.cpp/.hpp` | 現有 pipeline 完整保留，作為初始建圖的後端 |
| `src/libslic3r/SLA/SupportTreeMesher.hpp` | `get_mesh()` 函數直接被新架構複用 |
| `src/libslic3r/SLA/SupportTreeBuilder.hpp` 的各 node struct | `Head`、`Pillar`、`Bridge` 等幾何結構不變 |

---

## 七、最小可行實作路徑

```
Phase 1 - 視覺化基礎
    ├─ 建立 SupportGraph 資料結構
    ├─ 在 pipeline 完成後執行 to_graph() 轉換
    └─ 實作 load_object_support_graph()，以 per-node GLModel 顯示支撐

Phase 2 - 選取與識別
    ├─ GLVolume 加入 support_node_id
    ├─ Raycast hit-test 識別點選的 node
    └─ 選取時 highlight 對應 GLModel

Phase 3 - 互動編輯
    ├─ 拖曳移動 node（mark_dirty + rebuild + GLModel update）
    ├─ 點擊 model 表面新增 Tip node
    └─ Delete 鍵刪除選取的 node（children re-attach 邏輯）

Phase 4 - 匯出
    └─ 互動完成後，將 SupportGraph 轉回 merged TriangleMesh
       供後續切片與列印使用
```

Phase 1 完成後現有切片功能完全不受影響（最終 export 仍走原本的 merged mesh），後續 Phase 逐步加上互動能力。

---

## 八、現行 PhrozenOrca 演算法是否需要更動

### 結論

> **SupportTreeBuildsteps 的演算法邏輯零修改。** 需要改的只有 `Bridge` 和 `Pillar` 兩個 struct 補幾個 id 欄位，其餘都是在既有架構外面新增層次。

---

### 現有連結的完整性檢查

`SupportGraph.to_graph()` 轉換函數需要從 pipeline 的輸出重建父子拓撲。以下逐項檢查現有資料是否足夠：

| 從哪找到哪 | 現況 | 夠用嗎 |
|------------|------|--------|
| Head → 它對應的 Pillar | `Head.pillar_id` ✓ | ✓ |
| Head → 它對應的 Bridge | `Head.bridge_id` ✓ | ✓ |
| Pillar → 從哪個 Head 出發 | `Pillar.start_junction_id` + `starts_from_head` ✓ | ✓ |
| **Pillar → 哪些 Bridge 接在它身上** | `Pillar.bridges`（僅計數）✗ | **✗ 缺 id list** |
| **Bridge → 它的 source Head 是誰** | 無 ✗ | **✗ 完全缺失** |
| **Bridge → 它的 target Pillar 是誰** | 無 ✗ | **✗ 完全缺失** |
| **Crossbridge → 連接哪兩根 Pillar** | 無 ✗（只有座標） | **✗ 完全缺失** |
| **Pillar → 哪些 Pillar 被 cascade 連到它** | `Pillar.links`（僅計數）✗ | **✗ 缺 id list** |

`Bridge` struct 目前只儲存座標，沒有 source/target 的 id：

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuilder.hpp:168`

```cpp
struct Bridge : public SupportTreeNode {
    double r = 0.8;
    Vec3d  startp = Vec3d::Zero();
    Vec3d  endp   = Vec3d::Zero();
    // ← 沒有 source_head_id，沒有 target_pillar_id
};
```

`Pillar` 只記計數，沒有 id list：

**相關檔案：** `src/libslic3r/SLA/SupportTreeBuilder.hpp:128`

```cpp
struct Pillar : public SupportTreeNode {
    unsigned bridges = 0;  // ← 只知道有幾個，不知道是哪幾個
    unsigned links   = 0;  // ← 同上
    // 沒有 bridge_ids，沒有 linked_pillar_ids
};
```

---

### 需要補充的最小資料結構修改

**`SupportTreeBuilder.hpp` — 補充 id 欄位：**

```cpp
// Bridge 補上兩端的 id
struct Bridge : public SupportTreeNode {
    double r = 0.8;
    Vec3d  startp, endp;
    long   source_id = ID_UNSET;  // 新增：source Head 或 Junction 的 id
    long   target_id = ID_UNSET;  // 新增：target Pillar 或 Junction 的 id
};

// Pillar 補上反向 id list
struct Pillar : public SupportTreeNode {
    unsigned bridges = 0;
    unsigned links   = 0;
    std::vector<long> bridge_ids;        // 新增：哪些 Bridge 掛在此 Pillar
    std::vector<long> linked_pillar_ids; // 新增：哪些 Pillar cascade 連到此
};
```

**`SupportTreeBuilder.cpp` — `add_bridge()` 和 `increment_bridges()` 在記錄時同時填入 id：**

```cpp
// add_bridge(headid, endp) 填完後加：
m_pillars[target_pillar_id].bridge_ids.push_back(bridge.id);
bridge.source_id = headid;
bridge.target_id = target_pillar_id;

// increment_bridges() 改為同時記錄 id：
m_pillars[pillar.id].bridge_ids.push_back(bridge_id);
m_pillars[pillar.id].bridges++;
```

這只是在已有操作旁邊多記一筆 id，不改變任何決策邏輯與回傳值。

---

### 各層更動性質總表

| 檔案 | 需要改嗎 | 改的性質 |
|------|----------|----------|
| `SupportTreeBuildsteps.cpp/.hpp` | **不需要** | 演算法邏輯完全不動 |
| `SupportTreeMesher.hpp` | **不需要** | `get_mesh()` 直接複用 |
| `SupportTreeBuilder.hpp` — node struct | **需要小改** | `Bridge` 加 `source_id/target_id`；`Pillar` 加 `bridge_ids/linked_pillar_ids` |
| `SupportTreeBuilder.cpp` — `add_bridge()`、`increment_bridges()`、`increment_links()` | **需要小改** | 填入新增的 id 欄位，不影響回傳值與行為 |
| `SupportTree.hpp` | **需要小改** | 新增 `virtual retrieve_graph()` 虛函數宣告 |
| `SupportGraph.hpp/.cpp` | **新建** | 圖資料結構 + `to_graph()` 轉換 + `mark_dirty()` |
| `SLAPrint.hpp/.cpp` | **需要小改** | `SupportData` 加 `graph_data`，pipeline 後呼叫 `to_graph()` |
| `3DScene.cpp` | **需要新增** | `load_object_support_graph()`，舊路徑保留不動 |
| `GLCanvas3D.cpp` | **需要改** | Prepare tab 的支撐載入切換到新路徑 |
| `GLGizmoSLASupportEdit.hpp/.cpp` | **新建** | 互動 Gizmo |
