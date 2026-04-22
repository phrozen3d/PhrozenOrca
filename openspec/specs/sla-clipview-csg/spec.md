# Spec: sla-clipview-csg
## PhrozenOrca SLA — ObjectClipper 改用 CSG parts range

**文件版本**: v1.0
**日期**: 2026-04-22
**來源 change**: sla-csgmesh-architecture-migration
**狀態**: Implemented

---

## 1. 背景

原實作在 `ObjectClipper::on_update()` SLA 分支把整個 `hollow_mesh_with_holes`（CGAL 產生的合併大 mesh）
傳給 `MeshClipper`，造成每次 slider 移動都對大 mesh 執行 O(三角形數) 的 CPU 切片，是 Hollow Gizmo 卡頓的直接原因。

改為以 `get_parts_to_slice()` 取得 CSG parts，各 part 是小 mesh，`recalculate_triangles()` 成本大幅降低。

**座標系注意**：CSGPart 已嵌入 world transform（含 instance trafo）。
`render_cut()` 計算 `m_trafo = inst_trafo * clipper.second`；
為使截面平面保持在 world space，`clipper.second` 設為 `po->trafo().inverse()`，
使 `m_trafo ≈ Identity`，CSG parts 的 world-space 座標得以正確對齊。

---

## 2. Requirements

### Requirement: ObjectClipper SLA 路徑使用 CSG parts range
在 SLA printer 模式下，`ObjectClipper::on_update()` SHALL 以 `po->get_parts_to_slice()` 取得 CSG parts，並呼叫 `MeshClipper::set_mesh(range(partstoslice))` 走 CSG range 路徑，不再使用單一合併的 hollowed TriangleMesh。

#### Scenario: 進入 Hollow Gizmo 時 clipper 以 CSG parts 初始化
- **WHEN** 使用者開啟 SLA 模型的 Hollow Gizmo，`ObjectClipper::on_update()` 執行
- **THEN** `MeshClipper` SHALL 以 CSG parts range 初始化（`set_mesh(range(...))`），不使用 `m_sla_mesh_ptr`

#### Scenario: CSG parts 未就緒時不初始化 clipper
- **WHEN** `get_parts_to_slice()` 回傳空集合（pipeline 尚未執行 Assembly）
- **THEN** `ObjectClipper` SHALL 不建立 MeshClipper，`render_cut()` 的 `m_clp_ratio == 0` 條件觸發 early return

#### Scenario: 非 SLA printer 模式不受影響
- **WHEN** 使用者使用 FDM printer 模式開啟任何 Gizmo
- **THEN** `ObjectClipper::on_update()` SHALL 走現有的 model volumes 路徑，不觸及 CSG pipeline 邏輯

### Requirement: Clip view 截面視覺正確性
使用 CSG parts 路徑後，clip view 的截面填色、輪廓線、hollow 內腔顯示 SHALL 與使用單一合併 mesh 路徑時視覺等價。

#### Scenario: 截面填色正確
- **WHEN** Z-clip slider 移動至模型中段，`render_cut()` 執行
- **THEN** 截面 SHALL 以灰色填滿（`{0.25f, 0.25f, 0.25f, 1.0f}`），hollow 內腔部分 SHALL 為空（不填色）

#### Scenario: hollow 內腔輪廓正確
- **WHEN** hollowing 啟用，slider 位於 hollow interior 穿越的高度
- **THEN** clip view SHALL 顯示外殼截面與內腔截面各自的輪廓線，內腔不被填滿

#### Scenario: 排水孔截面正確
- **WHEN** slider 位於排水孔穿越的高度
- **THEN** clip view SHALL 在排水孔位置顯示孔洞（截面上的缺口），形狀與排水孔幾何一致

### Requirement: Clip view 渲染效能改善
使用 CSG parts 路徑後，`MeshClipper::recalculate_triangles()` 的耗時 SHALL 相比使用合併大 mesh 時明顯降低，因為各 CSG part 的三角形數遠少於合併後的 hollowed mesh。

#### Scenario: slider 拖動時 recalculate 耗時降低
- **WHEN** Z-clip slider 拖動，觸發 `recalculate_triangles()`，模型原始 mesh 為 M 個三角形
- **THEN** 每次 `recalculate_triangles()` 處理的三角形總數 SHALL 不超過各 CSG parts 三角形數之和，應遠小於合併 hollowed mesh 的 5–10× M
