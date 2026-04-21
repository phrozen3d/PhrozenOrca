## Context

PhrozenOrca 的 SLA print pipeline 繼承自 OrcaSlicer，在 `slaposDrillHoles` 步驟執行 CGAL 3D Mesh Boolean（`hollow_mesh_and_drill()`），將 hollow interior 與排水孔合併成一個 `hollow_mesh_with_holes`，後續所有步驟（切片、clip view）都使用這個合併大 mesh。

PrusaSlicer 在 2021–2023 年間已完成架構遷移，改用 `m_mesh_to_slice`（CSGPart multimap）：各步驟只把自己產生的 mesh 以 CSGType（Union / Difference）加入 multimap，切片時呼叫 `slice_csgmesh_ex()` 對各 part 分別切片並在 2D 合併。PhrozenOrca 已有 `libslic3r/CSGMesh/` 基礎設施（`SliceCSGMesh.hpp`、`ModelToCSGMesh.hpp` 等），但尚未整合進 SLA pipeline。

現有 `GLGizmosCommon.cpp` 的 `ObjectClipper::on_update()` 在 SLA 模式下把整個 hollowed mesh 傳給 `MeshClipper`，造成每次 slider 移動都對大 mesh 執行 O(三角形數) 的 CPU 切片（`recalculate_triangles()`），是 Hollow Gizmo 卡頓的直接原因。

## Goals / Non-Goals

**Goals:**
- 加入 `slaposAssembly` 步驟，建立 CSG parts 基礎架構
- `drill_holes()` 改走 CSG Difference path，消除 CGAL 3D Boolean 失敗風險
- `slice_model()` 改用 `slice_csgmesh_ex()`，從 2D 層級組合 hollow + drill
- `ObjectClipper` SLA 路徑改走 CSG range，clip view 改切小 part 而非大 mesh
- 切片結果幾何等價（與移植前差異 < 0.01mm）

**Non-Goals:**
- 修改支撐演算法（SupportTreeBuilder）本身
- 修改 Rasterization pipeline
- 修改 FDM 功能或 PhrozenOrca 客製化（BUILD_PHROZEN_ORCA 等）
- Support 介面調整（`get_mesh_to_print()` → `indexed_triangle_set`）—— 列為 Phase E，可暫緩

## Decisions

### D1：以 `std::multimap<SLAPrintObjectStep, csg::CSGPart>` 作為 CSG container

**選擇**：沿用 PrusaSlicer 的 `m_mesh_to_slice` multimap 設計，key 為步驟枚舉，value 為 `csg::CSGPart`。

**理由**：
- `clear_csg(map, step)` 可以按步驟精確清除，不影響其他步驟的 parts
- `range(map)` 可直接傳給 `slice_csgmesh_ex()`，介面零摩擦
- 對應的 `csg_inserter` helper 使程式碼簡潔

**替代方案**：用 `std::vector<csg::CSGPart>` 搭配手動清除 → 無法按步驟隔離，拒絕。

---

### D2：新增 `slaposAssembly` 作為 pipeline 第一步

**選擇**：在 `slaposHollowing` 之前插入 `slaposAssembly`（enum 值 0），其餘步驟值各 +1。

**理由**：
- PrusaSlicer 架構的 `mesh_assembly()` 負責把模型 volumes 轉為 CSG parts，這是後續所有步驟的基礎
- 讓 `hollow_model()` 和 `drill_holes()` 只需要 `clear_csg` 自己的 step 並加入新 parts，不需重建整個 multimap

**影響**：`OBJ_STEP_LEVELS`、`OBJ_STEP_LABELS` 陣列及 `Steps::execute()` 的 switch-case 需同步更新。

---

### D3：`hollow_model()` 保留 `m_hollowing_data->interior`，僅額外加入 CSG part

**選擇**：移植期間保留 `m_hollowing_data->interior`，同時將 interior mesh 以 `CSGType::Difference` 加入 `m_mesh_to_slice[slaposHollowing]`。等 Phase C（`slice_model` 移植）完成驗證後，再移除對 `hollow_mesh_with_holes` 的依賴。

**理由**：雙軌並行降低移植風險；若 Phase C 驗證失敗，可快速回退至舊路徑。

---

### D4：`drill_holes()` 以 `model_to_csgmesh(..., mpartsDrillHoles)` 取代 CGAL Boolean

**選擇**：用 `csg::model_to_csgmesh()` 的 `mpartsDrillHoles` flag 把排水孔幾何加入 `m_mesh_to_slice`，排水孔在 2D 切片時以 `diff_ex()` 處理。

**理由**：
- 2D polygon diff（Clipper2）不會因 mesh 幾何問題失敗
- 排水孔幾何（圓柱）是正規 mesh，`model_to_csgmesh` 可直接處理
- 移除 try/catch + voxelization fallback，簡化程式碼

**前置確認**：需在 Phase B 開始前驗證 PhrozenOrca 的 `sla::DrainHole` 結構與 `mpartsDrillHoles` 的轉換邏輯相容。

---

### D5：`ObjectClipper::on_update()` 改用 `get_parts_to_slice()` + CSG range

**選擇**：SLA 模式下以 `po->get_parts_to_slice()` 取得 CSG parts，呼叫 `mc->set_mesh(range(partstoslice))`，走 `slice_csgmesh_ex()` 路徑。

**理由**：
- `MeshClipper` 已有 `set_mesh(Range<It>)` template，支援 CSG range
- 各 part（shell、interior、drain holes）各自是小 mesh，`recalculate_triangles()` 切片成本大幅降低
- `m_sla_mesh_ptr` cache 可完全移除

**替代方案**：保留大 mesh 路徑並加入 Z-sorted 預排序優化 → 治標不治本，拒絕。

---

### D6：`generate_preview()` 採用 PrusaSlicer 的 VDB voxelization fallback 策略

**選擇**：每個 step 完成後呼叫 `generate_preview(po, step)`，用於 Gizmo 的 preview mesh 顯示，與切片邏輯解耦。

**理由**：preview mesh 可以是近似（voxelization），切片精度由 `slice_csgmesh_ex()` 保證，兩者分離。

## Implementation Findings (Phase B/C 實作紀錄)

### Finding 1：`mpartsDrillHoles` 在 PhrozenOrca 不可用

`ModelToCSGMesh.hpp` 的排水孔區段（drain holes section）已被 comment out，
原因是依賴 PrusaSlicer 的 free function `sla::transformed_drainhole_points(mo, trafo)`，
此函式在 PhrozenOrca 不存在（PhrozenOrca 使用 member method `po.transformed_drainhole_points()`）。

**修訂**：不使用 `model_to_csgmesh(..., mpartsDrillHoles)`，改為在 `drill_holes()` 中
手動迭代 `po.transformed_drainhole_points()`，以 `dhole.to_mesh()` 建立圓柱 mesh，
再用 `csg_inserter{po.m_mesh_to_slice, slaposDrillHoles}` 逐一加入。

---

### Finding 2：移除 CGAL 後排水孔消失（Phase B/C 兩次失敗）

**第一次嘗試（Phase B）**：移除 CGAL drilling，`hollow_mesh_with_holes` 只含 hollow
（無孔洞），`slice_model()` 仍用 `slice_mesh_ex(hollow_mesh_with_holes)` → 孔洞不出現。
原因明確：`slice_mesh_ex` 切的 mesh 本身就沒有孔洞。

**第二次嘗試（Phase C）**：同時改 `slice_model()` 為 `slice_csgmesh_ex`（含
`csgmesh_positive_bb` + `bounding_box(its, transform)` 新增多載），但孔洞仍不出現。
根本原因尚未完全確認，候選原因：
- Model CSG parts 儲存格式：**local-space mesh + instance transform**（via `model_to_csgmesh`）
- Drain hole CSG parts 儲存格式：**world-space mesh + Identity transform**（via `to_mesh()` after `transformed_drainhole_points()`）
- 兩者在 `slice_csgmesh_ex` 的 Z grid 對齊需要進一步驗證

---

### D4 修訂：雙軌並行策略（Dual-Path）

**原計畫（D4）**：直接移除 CGAL，由 `slice_csgmesh_ex` 接管排水孔的 2D 差集。

**修訂後執行策略**：
1. `drill_holes()` **保留 CGAL** 繼續產生 `hollow_mesh_with_holes`（確保切片完全正確）
2. `drill_holes()` **同時**將排水孔 mesh 以 CSG Difference parts 注冊到 `m_mesh_to_slice`
3. `slice_model()` 維持 `slice_mesh_ex(hollow_mesh_with_holes)`，不動
4. Phase C 升級為：先在**雙軌並行狀態**（3.0）驗證 `slice_csgmesh_ex` 路徑輸出與
   CGAL 路徑數值等價，確認後才切換並移除 CGAL

**理由**：零回退風險；先驗證 CSG 路徑幾何正確性，再移除 CGAL 舊路徑。

---

## Risks / Trade-offs

**[Risk] `slicegrid` Z 計算與舊版不一致，導致層數差異**
→ Phase C 前逐行對齊 slicegrid 計算邏輯（first layer height、layer height、Z correction offset）；Phase B 後先量測差異作為 baseline。

**[Risk] `csg::mpartsDrillHoles` 無法正確讀取 PhrozenOrca 的 `sla_drain_holes`**
→ **已確認發生**：見 Implementation Findings，已改為手動迭代。

**[Risk] `slaposAssembly` enum 插入造成已儲存 project 的 step 狀態失效**
→ SLA print state 在每次開啟 project 時重新計算，不持久化 step 索引值；風險低。

**[Risk] Phase D 後 clip view 效能未達預期（CSG parts 數量多）**
→ Phase D 有效能量測基準；若效能不足，可對 assembly parts 做預合併（positive parts merge）再傳給 MeshClipper。

**[Risk] Support tree raycasting 精度變化（Phase E 暫緩）**
→ Phase A–D 完成後若 support 功能正常，Phase E 可在支撐演算法大改版時一併處理；暫緩期間記錄於 MEMORY.md。

## Migration Plan

移植分五個 Phase，各自可獨立編譯驗證：

| Phase | 主要工作 | 可回退點 |
|-------|---------|---------|
| A | 加入 CSG 資料結構 + `mesh_assembly()` | 舊路徑完全保留 |
| B | `drill_holes()` 雙軌並行（保留 CGAL + 注冊 CSG parts） | Phase B 不動 CGAL，零回退風險 |
| C | `slice_model()` 改 `slice_csgmesh_ex()` | 切片結果數值驗證通過才合併 |
| D | `ObjectClipper` 改 CSG range | 只影響 GUI，不影響切片結果 |
| E | Support 介面調整（可暫緩） | 獨立 Phase，不阻塞 A–D |

**Rollback 策略**：每個 Phase 完成後建立 git commit；若 Phase C 驗證失敗，`git revert` 至 Phase B commit，舊路徑繼續運作。

## Open Questions

1. ~~**`model_to_csgmesh` 的 `mpartsDrillHoles` flag 在 PhrozenOrca 是否已實作**？~~ **已解答**：不可用，改為手動迭代（見 Implementation Findings Finding 1）。

2. **`generate_preview()` 的 VDB voxelization** 在 PhrozenOrca 是否有 OpenVDB dependency？若無，需替代方案（直接用 CGAL boolean 合併 positive parts 作為 preview mesh）。

3. **Phase E 暫緩條件**：Phase A–D 完成後，若 support raycasting 出現精度問題（support 點落在 hollow interior 內），則 Phase E 需提前執行。

4. ~~**Phase C 3.0 雙軌驗證方法**~~：**已解答**。drain hole（world-space + Identity）與 model parts（local-space + instance trafo）在 `slice_csgmesh_ex` 內均正確還原為 world-space，Z grid 對齊正確；三類測試（實心、hollow 無孔、hollow + 排水孔）均通過。

---

## Known Limitations

### `zcorrection_layers` 無 UI 入口

**狀態**：後端邏輯存在（`apply_zcorrection()` 在 `apply_printer_corrections()` 中呼叫），
但 UI 未串接：
- `PrintConfig.cpp` 的 label / tooltip / category 全部 comment out
- 沒有任何 SLA 材料 preset 檔案設定此參數
- 所有 preset 值為預設值 0（= 停用）

**影響**：使用者目前無法從 UI 啟用 Z correction；`apply_zcorrection` 永遠是 no-op。

#### Z Correction 參數的作用

**問題根源**：SLA UV 光固化時，光線會穿透當前層往下滲入樹脂（cross-layer bleed），
導致下方幾層也被部分固化，使模型在 Z 方向尺寸偏大，尤其 overhang 區域誤差明顯。

**修正原理**：切片完成後，對每一層取「本層與往下 N 層」的 2D 交集（intersection）：

```
Layer[i] = intersect(Layer[i], Layer[i-1], ..., Layer[i-N])
```

實作為 `intersect_layers(slices, layer_from, layers_down)`（ZCorrection.cpp:117）。
交集後每層只保留「連續 N+1 層都存在的輪廓」，有效縮小曝光面積，補償滲光。

**`zcorrection_layers = N` 的效果**：
- `N = 0`：停用，輸出原始切片（目前所有 preset 的狀態）
- `N = 1`：每層取與下一層的交集，移除單層懸空輪廓
- `N = 2`：每層取與下兩層的交集，效果更強，適合高滲光樹脂
- N 越大 → overhang 尺寸補償越激進，但薄壁細節可能消失

**副作用**：不改變層數，但縮小每層輪廓面積；薄壁（thickness < N 層高度）可能因交集結果為空而消失。

**後續動作**：若需開放給使用者，需在 PrintConfig.cpp 恢復 label/tooltip，
並在 SLA 材料設定 tab 加入對應的 UI 控制項。此工作與 CSGMesh 移植無關，可獨立排期。
