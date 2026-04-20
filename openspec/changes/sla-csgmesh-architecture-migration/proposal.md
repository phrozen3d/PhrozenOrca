## Why

PhrozenOrca 的 SLA Hollowing 流程依賴 CGAL 3D Mesh Boolean（`hollow_mesh_and_drill()`）合併 hollow interior 與排水孔，產生的單一大 mesh 在 clip view 渲染時觸發 O(三角形數) 的 CPU 切片，造成 Hollow Gizmo 卡頓；且 CGAL 對 degenerate geometry 容易失敗，現有程式碼需要 try/catch + voxelization fallback 兜底。PrusaSlicer 已改用 CSG 2D Slice 架構，在 2D 切片層級執行 Boolean，完全避開 3D mesh 合併；現在是跟進的時機，同步解決穩定性與效能問題。

## What Changes

- **新增** `slaposAssembly` pipeline 步驟，負責將模型 volumes 轉換為 CSG parts 存入 `m_mesh_to_slice`
- **新增** `SLAPrintObject::m_mesh_to_slice`（multimap）與 `m_preview_meshes`（per-step preview）資料結構
- **新增** `get_parts_to_slice()`、`get_mesh_to_print()`、`mesh_to_slice()` 方法
- **修改** `hollow_model()`：interior mesh 改以 `CSGType::Difference` 加入 `m_mesh_to_slice`，不再直接修改 TriangleMesh
- **修改** `drill_holes()`：移除 `hollow_mesh_and_drill()` CGAL 3D Boolean，改以 `model_to_csgmesh(..., mpartsDrillHoles)` 加入 CSG Difference parts
- **修改** `slice_model()`：以 `slice_csgmesh_ex()` 取代 `slice_mesh_ex(hollow_mesh_with_holes)` + interior diff
- **修改** `ObjectClipper::on_update()`（SLA 路徑）：以 `get_parts_to_slice()` + `set_mesh(range(...))` 取代單一大 hollowed mesh
- **移除** `m_sla_mesh_ptr` cache 欄位（不再需要）

## Capabilities

### New Capabilities

- `sla-csg-pipeline`: SLA print pipeline 的 CSG 分層架構，包含 `slaposAssembly` 步驟、`m_mesh_to_slice` multimap、以及 `get_parts_to_slice()` / `get_mesh_to_print()` 介面
- `sla-drill-holes-csg`: 排水孔處理改走 CSG Difference path，以 2D diff 取代 CGAL 3D Boolean，消除失敗路徑
- `sla-slice-model-csg`: `slice_model()` 改用 `slice_csgmesh_ex()` 對各 CSG parts 分別切片並在 2D 合併
- `sla-clipview-csg`: `ObjectClipper` SLA 路徑改走 CSG range，clip view 渲染效能改善

### Modified Capabilities

（無既有 spec 的 requirement 層級變更）

## Impact

- `src/libslic3r/SLAPrint.hpp` — `SLAPrintObjectStep` enum、`SLAPrintObject` 資料結構與方法
- `src/libslic3r/SLAPrintSteps.cpp` — `mesh_assembly()`、`hollow_model()`、`drill_holes()`、`slice_model()`
- `src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp/.cpp` — `ObjectClipper::on_update()`、移除 `m_sla_mesh_ptr`
- 依賴：`libslic3r/CSGMesh/`（已存在）、`libslic3r/CSGMesh/SliceCSGMesh.hpp`、`libslic3r/CSGMesh/ModelToCSGMesh.hpp`
- FDM 功能完全不受影響；PhrozenOrca 客製化（`BUILD_PHROZEN_ORCA` 等）不觸及
