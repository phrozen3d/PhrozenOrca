# Spec: sla-drill-holes-csg
## PhrozenOrca SLA — drill_holes() 雙軌並行策略

**文件版本**: v1.0
**日期**: 2026-04-22
**來源 change**: sla-csgmesh-architecture-migration
**狀態**: Implemented

---

## 1. 背景

原計畫（D4）要求直接移除 CGAL 3D Boolean，由 `slice_csgmesh_ex` 接管排水孔的 2D 差集。
實際移植發現兩次嘗試均導致孔洞消失（Implementation Findings, design.md）。
最終採用雙軌並行策略：保留 CGAL 確保切片正確，同時注冊 CSG parts 供 clip view 使用。

---

## 2. Requirements (D4 修訂後 — 雙軌並行策略)

> **修訂說明**：原始 spec 要求 SHALL NOT 執行 CGAL 3D Boolean。
> 根據 Implementation Findings（design.md），改為雙軌並行策略：
> 保留 CGAL 確保切片正確，同時注冊 CSG parts 供 Phase C/D 使用。

---

### Requirement: drill_holes() 保留 CGAL 並同時注冊 CSG parts

`drill_holes()` SHALL 繼續執行 CGAL 3D Boolean 以產生正確的 `hollow_mesh_with_holes`
（此 mesh 供 `get_mesh_to_print()` 使用，為支撐 raycasting 與 Gizmo preview 的來源）。

`drill_holes()` SHALL 同時將每個排水孔的圓柱 mesh（`dhole.to_mesh()`）以
`CSGType::Difference` 加入 `m_mesh_to_slice[slaposDrillHoles]`，
供 `slice_csgmesh_ex`（slice_model）與 ObjectClipper（clip view）使用。

---

### Requirement: CSG parts 數量與排水孔數量一致

`m_mesh_to_slice` 中 key 為 `slaposDrillHoles` 的 entries 數量
SHALL 等於 `po.transformed_drainhole_points()` 回傳的排水孔數量。

---

### Requirement: drill_holes() 執行前清除舊 CSG parts

`drill_holes()` 開頭 SHALL 呼叫 `clear_csg(po.m_mesh_to_slice, slaposDrillHoles)`，
確保重複執行不會累積舊的 entries。

---

## 3. Scenarios

#### Scenario: 無排水孔時不加入任何 CSG part

- **WHEN** `sla_drain_holes` 為空，`slaposDrillHoles` 執行
- **THEN** `m_mesh_to_slice` 中 key 為 `slaposDrillHoles` 的 entries SHALL 為零
- **AND** CGAL 路徑維持原本邏輯（無排水孔時跳過 CGAL Boolean）

---

#### Scenario: N 個排水孔加入 N 個 Difference parts

- **WHEN** 模型有 N 個排水孔，`slaposDrillHoles` 執行
- **THEN** `m_mesh_to_slice` SHALL 包含 N 個 key 為 `slaposDrillHoles`、
  `CSGType::Difference` 的 CSGPart
- **AND** CGAL Boolean 結果 `hollow_mesh_with_holes` 包含 N 個孔洞（原有行為不變）

---

#### Scenario: 幾乎相交的排水孔不崩潰

- **WHEN** 兩個排水孔的圓柱體幾何幾乎相交（中心距 < 排水孔直徑）
- **THEN** CSG parts 注冊 SHALL 正常完成（各自獨立加入，不做 3D 檢查）
- **AND** CGAL 端若相交導致 Boolean 失敗，行為維持原有的 error 處理邏輯

---

#### Scenario: 切片結果與移植前完全一致（回歸驗證）

- **WHEN** 執行任意 SLA 模型切片（含排水孔）
- **THEN** `m_model_slices` 層數與輪廓 SHALL 與 CSG 移植前完全相同
  （`slice_csgmesh_ex` 路徑已通過雙軌驗證，面積差 < 0.1%，位移 < 0.01mm）

---

## 4. 已知限制

`hollow_mesh_with_holes`（CGAL 產生）仍為以下功能的 mesh 來源，無法完全移除：
- `get_mesh_to_print()` → SupportData raycasting
- HollowedMesh Gizmo 3D 預覽
- Plater STL 匯出

移除 CGAL drill holes 的前提是實作 VDB voxelization 替代方案產生等價 mesh，
目前評估收益低於風險，暫不執行（見 design.md Risks/Trade-offs）。
