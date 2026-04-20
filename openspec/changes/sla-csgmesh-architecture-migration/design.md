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

## Risks / Trade-offs

**[Risk] `slicegrid` Z 計算與舊版不一致，導致層數差異**
→ Phase C 前逐行對齊 slicegrid 計算邏輯（first layer height、layer height、Z correction offset）；Phase B 後先量測差異作為 baseline。

**[Risk] `csg::mpartsDrillHoles` 無法正確讀取 PhrozenOrca 的 `sla_drain_holes`**
→ Phase B 前置確認步驟強制執行：用一個測試模型驗證 CSG parts 數量 = 排水孔數量。

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
| B | `drill_holes()` 改 CSG path | Phase B 失敗可還原 drill_holes() |
| C | `slice_model()` 改 `slice_csgmesh_ex()` | 切片結果數值驗證通過才合併 |
| D | `ObjectClipper` 改 CSG range | 只影響 GUI，不影響切片結果 |
| E | Support 介面調整（可暫緩） | 獨立 Phase，不阻塞 A–D |

**Rollback 策略**：每個 Phase 完成後建立 git commit；若 Phase C 驗證失敗，`git revert` 至 Phase B commit，舊路徑繼續運作。

## Open Questions

1. **`model_to_csgmesh` 的 `mpartsDrillHoles` flag 在 PhrozenOrca 是否已實作**？需在 Phase B 開始前確認 `ModelToCSGMesh.hpp` 中對應的 volume filter 邏輯。

2. **`generate_preview()` 的 VDB voxelization** 在 PhrozenOrca 是否有 OpenVDB dependency？若無，需替代方案（直接用 CGAL boolean 合併 positive parts 作為 preview mesh）。

3. **Phase E 暫緩條件**：Phase A–D 完成後，若 support raycasting 出現精度問題（support 點落在 hollow interior 內），則 Phase E 需提前執行。
