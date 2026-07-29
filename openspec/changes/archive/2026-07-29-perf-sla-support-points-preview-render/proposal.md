## Why

在 SLA 機台模式下開啟 Generate Support gizmo，於 Auto Support 面板選擇 **Points** 模式並按下 **Apply** 後，模型底部生成數百個支撐點，此時單純用滑鼠旋轉視角即出現嚴重延遲；切換到 Manual Support 模式後同樣卡頓。

延遲全部來自 `GLGizmoSlaSupports::render_points()`（`GLGizmoSlaSupports.cpp:645-728`）的 per-point 迴圈。該迴圈對 **每一個支撐點、每一幀** 重做四類本可攤提的工作，其中兩類佔了絕大部分成本：

| # | 位置 | 每幀次數 | 內容 | 成本佔比 |
|---|---|---|---|---|
| A | `:697` | N × 5~6 | `Tab::get_field()` 走訪全部 page 的線性搜尋 + `wxTextCtrl::GetValue()` 原生控制項文字讀取 | ~10-20% |
| B | `:683` / `:686` | N | `MeshRaycaster::get_closest_point()` AABB tree 最近點查詢，無快取 | ~5% |
| **C** | `:701-703` | **N** | **`sla::get_mesh(head, 24)` 重新生成 pinhead 網格** | **~40%** |
| **D** | `:708-723` | **N** | **`GLModel::reset()` 銷毀 VBO/IBO → `init_from()` 去索引化 → `send_to_gpu()` 重新配置上傳** | **~40%** |

以 N = 500 實測量級推算，C + D 每幀等於：約 45 萬個三角形重新生成、約 136 萬個頂點展開與 bounding box 計算、1,500 次 GL 呼叫、以及約 35 MB（啟用 contact sphere 時約 72 MB）的 GPU buffer 配置／上傳／銷毀。這是「旋轉視角即 lag」的直接來源。

D 之所以無法避免，是因為 `GLModel::init_from()` 開頭即為 `if (is_initialized()) { assert(false); return; }`（`GLModel.cpp:443`）——現行寫法**被迫**每幀先 `reset()` 再重建，無法重複使用既有 buffer。

### 為什麼 Points 與 Manual 都卡、Structure 不卡

`fix-sla-support-points-preview-mode-gate` 在 `render_points()` 入口加入的 view-mode gate（`:581`）為 `if (m_show_support_structure && !m_editing_mode) return;`。Structure 模式因此 early return 不進入迴圈；Points 模式與 Manual 編輯模式共用同一個迴圈（編輯模式下 `!m_editing_mode` 為 false 使 gate 失效），故兩者症狀完全一致。該 gate 並非本問題的肇因，成本一直存在，只是 Structure 被擋掉後對比才明顯。

### 修法可行的關鍵前提（已驗證）

`preview_sla_head_for_point()` 中的 `mesh_pen` 是**無條件**取用 `sp` 的：

```cpp
point_head_penetration_mesh_mm(sp, live_pen, pin_r, contact_r)
    → sp.head_penetration_mm >= 0 ? sp.head_penetration_mm : live_pen
```

只要有任一點的 `head_penetration_mm >= 0`，幾何快取的 key 就會發散。查 `SupportPoint.hpp:38-52` 確認 `SUPPORT_POINT_USE_PRESET = -1.f` 為預設值且 auto 生成路徑不會覆寫它，因此：

- **Points（非編輯）模式**：`use_stored_geometry` 恆為 false，全部點收斂為 **1 組幾何參數**。
- **Manual（編輯）模式**：auto 點（island / slope）仍共用該組參數，僅 `manual_add && has_explicit_geometry()` 的點各自分岔。

兩種模式皆成立，因此單一修法可同時消除兩個症狀。

## What Changes

**依效益排序，而非依風險排序**——第 1 項是唯一能帶來體感改善的修改，其餘三項單獨施作幾乎無感，僅為順帶清理。

### 1. Pinhead 幾何與 GLModel 依參數快取（主要修正，消除 C + D）

- 於 `SupportTreeMesher.hpp` 抽出 `head_mesh_body()`，回傳 **local frame**（原點在 head 錨點、軸向 −Z）的網格，即現行 `head_mesh_local()` 在最後 quaternion 旋轉與 `pos` 平移之前的結果；`head_mesh_local()` 改為呼叫它再套用旋轉平移，切片管線行為零變更。
- 於 gizmo 新增以量化幾何參數為 key 的 `GLModel` 快取，幾何參數相同的點共用同一個實例。
- 擺放責任從「烘進頂點」移到 model matrix，且必須與現行路徑算出**逐點相同的世界座標**。
- 快取於 gizmo 關閉、點集被取代、live 幾何參數變動時失效。

### 2. Process tab live 參數提到迴圈外（消除 A）

一次性讀取 `support_segment_length` / `support_head_penetration` / `support_head_front_diameter` / `support_head_back_diameter` / `support_contact_diameter` / `support_contact_type`，於整個迴圈重複使用。live 語意保留：使用者在 Process tab 邊改邊看仍於下一幀反映。`update_point_raycasters_for_picking_transform()` 適用同一規則。

### 3. Auto 點表面法線快取（消除 B）

非編輯模式下快取 `get_closest_point()` 結果，於 `m_normal_cache` 內容被取代時失效。編輯模式維持既有 `m_editing_cache[i].normal == Vec3f::Zero()` 判斷，語意不變。

### Non-goals

- **不移除永遠 inactive 的 cone picking raycaster。** `register_point_raycasters_for_picking()`（`:2351`）為每點註冊 sphere + cone 兩個 raycaster，其中 cone 在 `:2412` 永遠 `set_active(false)` 且 transform 始終停留在註冊時的 `Identity()`。它看似冗餘，實際上是**已回報的 picking 缺陷所需的基礎設施**——目前命中判定只靠半徑約 `r_pin`（預設 0.2 mm）的 sphere，使用者只能點到 pinhead 頂端的球，外露的錐體完全點不到。該缺陷將由 change `fix-sla-support-point-cone-picking` 透過「為 cone raycaster 補上 transform 並啟用」修正。本 change SHALL NOT 移除或改動該註冊。
- 不改動 Structure 模式（`GLGizmoSlaBase::render_volumes()`）、支撐生成 backend、Pad / Hollow / Drill / Prepare Z-clip 路徑。
- 不改變 Points preview 的視覺外觀、顏色規則、clipping 行為，或 hover / click 的命中判定結果。
- 不改變 `fix-sla-support-points-preview-mode-gate` 建立的 view-mode gate 與位置／尺寸拆解約定；本 change 建立在其之上。
- 不引入 GPU instancing、LOD 或 frustum culling。若上述修改後仍不足，另開 follow-up。
- 不為 `Tab::get_field()` 建立全域索引（影響整個 Tab 系統，範圍遠大於本 change）。
- 不處理 Structure mode undo/redo async reslice 不刷新（[KB-2]）。

## Capabilities

### New Capabilities

- `sla-support-points-preview-performance`：SLA Support gizmo 之 Points preview render 路徑的每幀成本上界。涵蓋 UI 欄位讀取次數、AABB 法線查詢次數、pinhead 網格建構次數、GPU buffer 生命週期，以及「這些最佳化不得改變可見結果與命中判定」的等價性不變式。

### Modified Capabilities

<!-- 無。`sla-support-points-preview` 由尚未 archive 的 fix-sla-support-points-preview-mode-gate 定義，本 change 不修改其任何 requirement，僅在其行為完全不變的前提下改變實作成本。 -->

## Impact

- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`
  - `render_points()` — 幾何／GLModel 快取、擺放改由 model matrix 承擔、live 參數提到迴圈外、法線快取
  - `preview_sla_head_for_point()` / `process_top_float_live()` / `process_contact_type_is_sphere()` — 新增一次性參數讀取路徑，保留現行多載
  - `update_point_raycasters_for_picking_transform()` — 參數提到迴圈外
  - `on_set_state()` 與所有 `m_normal_cache` 寫入點 — 快取失效
- **Primary**：`src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp` — 新增快取成員與相關型別
- **Secondary**：`src/libslic3r/SLA/SupportTreeMesher.hpp` — 抽出 `head_mesh_body()`；`head_mesh_local()` / `get_mesh(Head)` / `get_mesh_preview(Head)` 對外行為不變
- 不影響 `GLGizmoSlaBase`、支撐生成 backend、`GLModel` 本身
- 無 public API、檔案格式或 profile 變更
