# SLA 支撐互動編輯功能需求分析

## 前言

本文件為 [sla_support_graph_architecture.md](sla_support_graph_architecture.md) 的延續。
前文已完成以下分析：
- SLA 支撐樹的現有 Pipeline 架構（第一～五章）
- SupportGraph 新架構與現有架構的差異（第六章）
- 需要更動的範圍與各層更動性質（第七章）
- 現行演算法的更動幅度與資料結構缺口（第八章）

**本文件以前文的 SupportGraph 架構為基礎**，針對三項互動編輯功能需求，分析各需求的行為設計、每 frame 計算成本、缺少的機制，以及需要更動的檔案範圍。

前文結論要點（與本文直接相關）：
- `SupportTreeBuildsteps` pipeline 邏輯**完全不動**
- 互動功能全部在 `SupportGraph` 和新 Gizmo 層實現
- `Bridge` 和 `Pillar` struct 補充 id 欄位是唯一需要改的後端資料結構（詳見前文第八章）
- `SupportTreeMesher.hpp` 的 `get_mesh()` 系列函數可直接複用

---

## 需求一：新增接觸點 — 即時預覽完整支撐長法

### 使用者操作

滑鼠在 model 表面移動時，即時顯示若在此處新增支撐，整根支撐會如何長出（接觸點、往下連接、到 ground 或其他柱子）。點擊確認後正式加入。

### 預覽需要顯示的內容

```
model 表面 hit 點
    │
    ├─ [Tip] 接觸球體（Head）跟著滑鼠走
    │
    ├─ [Edge] 往下延伸的 cylinder
    │   ├─ 情況 A：附近有既有 Branch/Pillar node
    │   │         → 顯示斜向 Bridge 連到該 node
    │   │
    │   ├─ 情況 B：可直達 ground，無阻擋
    │   │         → 顯示垂直 Pillar + Pedestal
    │   │
    │   └─ 情況 C：需要 Bridge 繞過障礙再落地
    │             → 顯示斜向 Bridge + Pillar
    │
    └─ 全部為 ghost（未確認），左鍵點擊才寫入 SupportGraph
```

### 每 frame 執行的計算

| 步驟 | 計算內容 | 成本 |
|------|----------|------|
| `unproject_on_mesh()` | 滑鼠 → model 表面交點 | 低（已有，見 `GLGizmoSlaSupports.cpp:268`） |
| `spatial_index.query()` | 找最近既有 node | 低（KD-tree O(log N)） |
| 幾何判斷 | 距離 + 角度是否在連接範圍內 | 極低（向量運算） |
| `get_mesh(Bridge/Pillar)` | 算 ghost cylinder/sphere mesh | 低（< 1ms，複用 `SupportTreeMesher.hpp`） |
| `bridge_mesh_intersect()` | 8 ray cast 確認路徑不穿模 | 中（可接受） |

> NLopt 最佳化（`connect_to_ground` 精確路由，位於 `SupportTreeBuildsteps.cpp`）
> **不在 MouseMove 裡跑**，在左鍵確認後才執行一次。

### 現有可參考的實作

- `GLGizmoSlaSupports::unproject_on_mesh()` — 滑鼠 raycast 到 mesh 表面（`GLGizmoSlaSupports.cpp:268`）
- `GLGizmoBrimEars` 的 `render_hover_point` 機制 — 滑鼠移動時暫存預覽點不寫入 cache 的模式（`GLGizmoBrimEars.hpp:111`、`GLGizmoBrimEars.cpp:357`）

### 需要補充的機制（前文 SupportGraph 架構中尚未涵蓋）

| 機制 | 說明 |
|------|------|
| `SupportGraph.spatial_index` | KD-tree，查詢最近既有 node，供即時路由判斷 |
| `GhostPreview` 結構 | 暫存 tip node + edge mesh，不寫入 graph，每 frame 更新 |
| 輕量路由工具函數 | 從 `SupportTreeBuildsteps` 抽出 `connect_to_ground()` 和 `search_pillar_and_connect()` 作為可獨立呼叫的工具（不改演算法邏輯） |
| Gizmo `Moving` 事件 | 每 frame 更新 ghost；`LeftDown` 時確認寫入並觸發精確路由 |

`GhostPreview` 建議結構：

```cpp
struct SupportGraph {
    std::vector<SupportGraphNode> nodes;
    std::unordered_map<int, int>  support_point_to_node;
    PointIndex                    spatial_index;   // 新增

    struct GhostPreview {
        std::optional<SupportGraphNode>      tip;
        std::optional<indexed_triangle_set>  edge_preview;
        int target_node_id = -1;
    } ghost;
};
```

### 需要更動的檔案

| 檔案 | 更動內容 |
|------|----------|
| `src/libslic3r/SLA/SupportGraph.hpp` | 新增 `spatial_index`、`GhostPreview` 結構定義 |
| `src/libslic3r/SLA/SupportGraph.cpp` | 新增 `update_ghost_preview(hit_pos)` 函數 |
| `src/libslic3r/SLA/SupportTreeBuildsteps.hpp/.cpp` | 抽出 `connect_to_ground()`、`search_pillar_and_connect()` 為獨立可呼叫工具（不改演算法邏輯） |
| `src/slic3r/GUI/Gizmos/GLGizmoSLASupportEdit.cpp`（新建） | `Moving` 事件：呼叫 `update_ghost_preview()`；`LeftDown`：確認寫入 graph |

---

## 需求二：拖曳既有接觸點 — 即時預覽連接變化

### 使用者操作

點選已生成的支撐接觸點（Tip node），拖曳到 model 表面新位置，過程中即時看到整根支撐（Bridge、Pillar）隨之變形或重新路由。放開滑鼠後確認。

### 預覽分兩個層級同時運作

**層級 A（始終執行）：幾何預覽**

- Tip 球體跟著滑鼠走
- 往 current parent 的 cylinder 長度、角度即時更新
- 不做任何路由判斷，純幾何，每 frame 都跑

**層級 B（觸發條件成立時執行）：輕量路由更新**

```
觸發條件（二擇一）：
    ① distance(new_hit, current_parent) > max_bridge_length_mm
    ② slope_angle(new_hit → current_parent) < bridge_slope

代表原本的 parent 已無法連接，觸發後：
    spatial_index.query(new_hit) → 找新的 parent candidate
    幾何判斷新 parent 是否可連接
    → 更新 ghost edge target（不寫 SupportGraph）
```

**放開滑鼠後（確認）：**

```
分類不變（ground-facing 仍為 ground-facing）
    → mark_dirty(node_id) 直接更新幾何

分類改變（ground-facing ↔ model-facing）
    → 對此 node 執行局部 re_route_node()（含 NLopt 最佳化，只跑一次）
    → 結果寫入 SupportGraph

rebuild_dirty_meshes() → 更新對應 GLModel
ghost 清除
```

### 每 frame 執行的計算

| 操作 | 計算內容 | 成本 |
|------|----------|------|
| 層級 A 更新 | 一段 cylinder mesh 重算（複用 `get_mesh(Bridge)`） | 極低（< 1ms） |
| 觸發條件判斷 | 距離 + 角度計算 | 極低 |
| 層級 B：KD-tree query | 找新 parent | 低 |
| 層級 B：新 edge mesh | 算預覽 cylinder | 低 |
| 確認時 NLopt（若需要） | 精確路由最佳化 | 中（只在 MouseUp 執行一次） |

### 與需求一的關係

需求一補充的機制在需求二中**完全複用**，無需重複新增：

| 機制 | 需求一補充 | 需求二複用方式 |
|------|-----------|----------------|
| `GhostPreview` | 暫存新增預覽 | 拖曳中的 ghost edge 更新 |
| `spatial_index` | 查最近 node | 觸發層級 B 時查新 parent |
| 輕量路由工具函數 | 新增時路由判斷 | 拖曳結束時 re-route |

### 額外需要補充的機制

| 機制 | 說明 |
|------|------|
| `hover_id` → `SupportGraphNode` 對應 | 點選時識別被選取的是哪個 node（依賴前文第七章的 `GLVolume.support_node_id` 欄位） |
| `re_route_node(node_id)` | 拖曳結束後若分類改變，對單一 node 執行局部重新路由，不觸發整棵樹重算 |
| Undo/Redo 快照 | 拖曳開始前（`on_start_dragging()`）儲存 node 的舊 position 與 parent_id，供 Undo 回復 |

### 現有可參考的實作

- `GLGizmoSlaSupports::on_update()` — 拖曳中更新 support_point.pos 到 mesh 新交點（`GLGizmoSlaSupports.cpp:520`），目前拖曳結束後呼叫 `reslice_SLA_supports()` 整棵重算，改為 `re_route_node()` 局部重算
- `GLGizmoSlaSupports::on_start_dragging()` / `on_stop_dragging()` — Undo 快照的觸發點（`GLGizmoSlaSupports.cpp:926`）

### 需要更動的檔案

| 檔案 | 更動內容 |
|------|----------|
| `src/libslic3r/SLA/SupportGraph.hpp/.cpp` | 新增 `re_route_node(node_id)` 函數 |
| `src/slic3r/GUI/3DScene.hpp` | `GLVolume` 加入 `support_node_id` 欄位（前文第七章已列，此處確認依賴） |
| `src/slic3r/GUI/Gizmos/GLGizmoSLASupportEdit.cpp` | `on_start_dragging()`：記錄舊狀態；`on_dragging()`：層級 A/B ghost 更新；`on_stop_dragging()`：確認寫入，視情況呼叫 `re_route_node()` |

---

## 需求三：刪除既有接觸點 — 局部移除與拓撲修復

### 使用者操作

選取已生成的接觸點（Tip node），按 Delete 鍵移除。需移除該點對應的幾何，並根據連接情況決定是否保留或修剪上方的 Branch node。

### 刪除後需處理的三種情況

**情況 A：Tip 所連的 Branch node 還有其他子節點**

```
刪除前：              刪除後：
Branch P              Branch P
├─ Tip N（刪除）      └─ Tip M（不受影響）
└─ Tip M
```

只需移除 N 和 N→P 的 edge mesh。P 保持不變，`mark_dirty(P)` 更新子節點計數。

**情況 B：Tip 所連的 Branch node 刪除後變為冗餘節點**

```
刪除前：              刪除後：
Branch GP             Branch GP
└─ Branch P           └─（P 無子節點 → 可修剪）
   └─ Tip N（刪除）
```

P 剩下 0 個子節點 → 失去存在意義 → 執行 `prune_branch(P)`：
- 移除 P 及 P→GP 的 edge
- `mark_dirty(GP)` 讓 GP 重新判斷是否需要往下延伸

**情況 C：Tip 直接連接 Root（Pillar），刪除後 Pillar 無任何子節點**

```
刪除前：                   刪除後：
Root/Pillar（落地）        Root → 無子節點 → 整根移除
└─ Tip N（刪除）
```

呼叫 `remove_pillar(root_id)`：移除 Pillar、Pedestal，並從 `spatial_index` 清除。

### 執行流程

```
Delete 鍵按下
    ↓
1. Undo 快照：儲存 N 及所有祖先節點的狀態

2. 從 SupportGraph 移除 N
   parent.children_ids.remove(N.id)

3. 判斷情況：
   if parent.children_ids.empty():
       if parent.type == Root → remove_pillar(parent.id)    [情況 C]
       else                   → prune_branch(parent.id)     [情況 B]
   else:
       mark_dirty(parent)                                    [情況 A]

4. rebuild_dirty_meshes()
   → 只重算受影響節點的 mesh

5. 更新對應的 GLModel（只重傳 dirty 的 GPU buffer）
```

### 是否需要重新路由

| 情況 | 是否需要路由重算 | 說明 |
|------|----------------|------|
| 情況 A | 不需要 | 只移除幾何，拓撲不變 |
| 情況 B（修剪冗餘 Branch） | 輕量幾何判斷 | 判斷 GP 是否需要往下延伸，不需 NLopt |
| 情況 C（整根 Pillar 消失） | 不需要 | 直接移除，不影響其他 Pillar |
| 刪除的是 Branch node（中間節點） | 需要 re-attach | children 重新掛到 parent，重算每個 child 的 edge mesh |

### 需要補充的機制（前文 SupportGraph 架構中缺少的）

| 機制 | 說明 |
|------|------|
| `prune_branch(node_id)` | 情況 B 的修剪邏輯，移除冗餘 branch 並修復 parent 連接 |
| `remove_pillar(node_id)` | 情況 C 整根移除，同步清理 Pedestal 和 `spatial_index` |
| 刪除前 Undo 快照 | 儲存被刪節點及其所有祖先的狀態，供 Undo 回復整棵局部子樹 |

### 需要更動的檔案

| 檔案 | 更動內容 |
|------|----------|
| `src/libslic3r/SLA/SupportGraph.hpp` | 宣告 `remove_node()`、`prune_branch()`、`remove_pillar()` |
| `src/libslic3r/SLA/SupportGraph.cpp` | 實作上述三個函數及刪除後的 dirty 標記邏輯 |
| `src/slic3r/GUI/Gizmos/GLGizmoSLASupportEdit.cpp` | `Delete` 事件：儲存 Undo 快照 → 呼叫 `remove_node()` → `rebuild_dirty_meshes()` → 更新 GLModel |

---

## 三個需求的共用機制彙整

| 機制 | 需求一 | 需求二 | 需求三 |
|------|:------:|:------:|:------:|
| `SupportGraph.spatial_index` | ✓ 查最近 node | ✓ 查新 parent | — |
| `GhostPreview` 結構 | ✓ 新增預覽 | ✓ 拖曳預覽 | — |
| 輕量路由工具函數 | ✓ 即時路由判斷 | ✓ 觸發條件 + re-route | — |
| `mark_dirty` + `rebuild_dirty_meshes()` | ✓ 確認後更新 | ✓ 拖曳結束更新 | ✓ 刪除後更新 |
| `GLVolume.support_node_id` | — | ✓ 識別被選 node | ✓ 識別被刪 node |
| Undo/Redo 快照 | ✓ 確認前可取消 | ✓ 拖曳開始前儲存 | ✓ 刪除前儲存 |

---

## 跨文件更動範圍總表

> 前文（`sla_support_graph_architecture.md` 第七、八章）已列出基礎架構的更動範圍。
> 本表僅列出三項互動功能**額外新增**的更動，不重複前文已有的項目。

| 檔案 | 新增內容 | 所屬需求 |
|------|----------|----------|
| `src/libslic3r/SLA/SupportGraph.hpp` | `spatial_index`、`GhostPreview`、`re_route_node()`、`prune_branch()`、`remove_pillar()`、`remove_node()` 宣告 | 一、二、三 |
| `src/libslic3r/SLA/SupportGraph.cpp` | 上述函數實作 + `update_ghost_preview()` | 一、二、三 |
| `src/libslic3r/SLA/SupportTreeBuildsteps.hpp/.cpp` | 抽出 `connect_to_ground()`、`search_pillar_and_connect()` 為獨立工具（不改演算法邏輯） | 一、二 |
| `src/slic3r/GUI/Gizmos/GLGizmoSLASupportEdit.hpp/.cpp`（新建） | Moving / LeftDown / on_start_dragging / on_stop_dragging / Delete 事件處理 | 一、二、三 |
