# Spec: sla-csgmesh-architecture-migration
## PhrozenOrca SLA — 從 3D Boolean 移植至 CSG 2D Slice 架構

**文件版本**: v1.0  
**日期**: 2026-04-20  
**作者**: Willie Chen / Claude  
**狀態**: Proposed

---

## 1. 背景與動機

### 1.1 現況問題

PhrozenOrca 目前的 SLA Hollowing 流程繼承自 OrcaSlicer，採用傳統的 **3D Mesh Boolean 合併**方式：

```
slaposHollowing  → generate_interior()  → m_hollowing_data->interior
slaposDrillHoles → hollow_mesh_and_drill() [CGAL 3D Boolean]
                 → hollow_mesh_with_holes（合併大 mesh）
slaposObjectSlice → slice_mesh_ex(hollow_mesh_with_holes)
```

已知缺陷：
- CGAL 3D Boolean 對 degenerate geometry（薄壁、幾乎相切、非 manifold）容易失敗
- `drill_holes()` 有 try/catch + voxelization fallback，代表已知有失敗路徑
- 合併後的 hollowed mesh 三角形數為原始 mesh 5–10×，造成 `ObjectClipper::render_cut()` 渲染卡頓
- `SLAPrintObject::get_parts_to_slice()` 不存在，無法走 CSG 切片路徑

### 1.2 PrusaSlicer 的改善架構

PrusaSlicer 採用 **CSG 2D Slice** 架構，在 2D 切片層級執行 Boolean：

```
slaposAssembly   → m_mesh_to_slice[Union]      = 原始模型各部件
slaposHollowing  → m_mesh_to_slice[Difference] = interior mesh（獨立加入）
slaposDrillHoles → m_mesh_to_slice[Difference] = 排水孔幾何（獨立加入）
slaposObjectSlice → slice_csgmesh_ex() → 各部件分別切片 → 2D union/diff 合併
```

優點：
- 完全消除 3D CGAL Boolean 失敗風險
- 各 part 保持獨立（小 mesh），切片時不需掃描合併大 mesh
- `ObjectClipper` 走 CSG range 路徑，clip view 效能改善
- 失敗影響隔離在單一 Z 層，不汙染整個 mesh

---

## 2. 影響分析

### 2.1 各功能影響摘要

| 功能 | 現況 | 移植後 | 影響 |
|------|------|--------|------|
| **Drill Holes** | CGAL 3D Boolean，有失敗路徑 | CSG Difference part，2D diff | ✅ 純改善 |
| **Preview 切片品質** | 等價，但依賴 3D boolean 結果 | 2D CSG，不累積 3D 錯誤 | ✅ 改善穩定性 |
| **Hollow Clip View 效能** | 切大型 hollowed mesh（5–10×） | 切各自小 part | ✅ 效能改善 |
| **Support Points Raycasting** | `SupportData(TriangleMesh&)` | `SupportData(indexed_triangle_set&)` | ⚠️ 介面需調整 |
| **Z Correction / Layer Height** | 現有邏輯 | `slicegrid` 計算需驗證一致性 | ⚠️ 需驗證 |
| **SLA Pipeline 相容性** | OrcaSlicer 繼承 | 需加入 `slaposAssembly` 步驟 | ⚠️ 流程變更 |

### 2.2 不受影響的功能

- 支撐演算法本身（SupportTreeBuilder）
- Rasterization（slaposRasterize）
- Z correction 計算邏輯
- FDM 功能（完全無關）

---

## 3. 技術規格

### 3.1 需要新增 / 修改的元件

#### 3.1.1 `src/libslic3r/SLAPrint.hpp` — SLAPrintObject 類別

新增欄位：
```cpp
// 新增 CSG mesh container（對應 PrusaSlicer m_mesh_to_slice）
std::multimap<SLAPrintObjectStep, csg::CSGPart> m_mesh_to_slice;

// 新增 per-step preview mesh
std::map<SLAPrintObjectStep, std::shared_ptr<const indexed_triangle_set>> m_preview_meshes;
```

新增方法：
```cpp
// 取得到指定步驟為止的 CSG parts
std::vector<csg::CSGPart> get_parts_to_slice() const;
std::vector<csg::CSGPart> get_parts_to_slice(SLAPrintObjectStep untilstep) const;

// 取得合併後的 preview mesh（用於 support 和 raycasting）
const std::shared_ptr<const indexed_triangle_set>& get_mesh_to_print() const;

// 取得指定步驟的 CSG range（供 slice_csgmesh_ex 使用）
auto mesh_to_slice(SLAPrintObjectStep step) const;
```

修改 `SLAPrintObjectStep` enum，新增 `slaposAssembly`：
```cpp
enum SLAPrintObjectStep : unsigned int {
    slaposAssembly,       // 新增：建立 CSG parts from model volumes
    slaposHollowing,
    slaposDrillHoles,
    slaposObjectSlice,
    slaposSupportPoints,
    slaposSupportTree,
    slaposPad,
    slaposSliceSupports,
    slaposCount
};
```

#### 3.1.2 `src/libslic3r/SLAPrintSteps.cpp` — 各步驟實作

**新增 `mesh_assembly()`**：
```cpp
void SLAPrint::Steps::mesh_assembly(SLAPrintObject &po)
{
    po.m_mesh_to_slice.clear();
    po.m_supportdata.reset();
    po.m_hollowing_data.reset();

    csg::model_to_csgmesh(*po.model_object(), po.trafo(),
                          csg_inserter{po.m_mesh_to_slice, slaposAssembly},
                          csg::mpartsPositive | csg::mpartsNegative | csg::mpartsDoSplits);

    generate_preview(po, slaposAssembly);
}
```

**修改 `hollow_model()`**：
- 移除：直接修改 hollowed TriangleMesh
- 改為：把 interior mesh 加入 `m_mesh_to_slice` 作為 `CSGType::Difference`

**修改 `drill_holes()`**：
- 移除：`hollow_mesh_and_drill()` CGAL 3D Boolean
- 改為：`model_to_csgmesh(..., csg::mpartsDrillHoles)` 加入 CSG parts

**修改 `slice_model()`**：
- 移除：`slice_mesh_ex(hollow_mesh_with_holes)` + interior diff
- 改為：`slice_csgmesh_ex(range(po.m_mesh_to_slice), slicegrid, params)`

#### 3.1.3 `src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp` — ObjectClipper

**修改 `ObjectClipper::on_update()`**（SLA 路徑）：
```cpp
// 現在（PhrozenOrca）
sla_mc->set_mesh(new_sla_mesh_ptr->its);  // 大型合併 mesh

// 改為（PrusaSlicer 架構）
auto partstoslice = po->get_parts_to_slice();
if (!partstoslice.empty()) {
    mc = std::make_unique<MeshClipper>();
    mc->set_mesh(range(partstoslice));     // CSG range，各自小 mesh
    mc_tr = Geometry::Transformation{po->trafo().inverse().cast<double>()};
}
```

移除不再需要的 `m_sla_mesh_ptr` cache 欄位（`GLGizmosCommon.hpp`）。

---

## 4. 移植計劃（分階段）

### Phase A：基礎架構建立（低風險）

**目標**：加入 CSG 基礎設施，不移除舊路徑，確保可雙向切換。

#### A.1 加入資料結構

修改檔案：`src/libslic3r/SLAPrint.hpp`

- 在 `SLAPrintObject` 加入 `m_mesh_to_slice`（multimap）、`m_preview_meshes`（map）
- 在 `SLAPrintObjectStep` enum 加入 `slaposAssembly`（值為 0，其餘步驟 +1）
- 加入 `get_parts_to_slice()` 宣告

#### A.2 實作 `mesh_assembly()`

修改檔案：`src/libslic3r/SLAPrintSteps.cpp`

- 實作 `mesh_assembly(SLAPrintObject&)`
- 在 `Steps::execute()` 的 switch-case 加入 `slaposAssembly` 分支
- 確認 `OBJ_STEP_LEVELS` / `OBJ_STEP_LABELS` 陣列對應更新

#### A.3 驗證

| 驗證項目 | 方法 | 通過條件 |
|---------|------|---------|
| 編譯通過 | `build_release_vs2022.bat slicer` | 無編譯錯誤 |
| 現有 SLA 功能不受影響 | 開啟 SLA 模型執行切片 | 切片結果與移植前相同 |
| CSG parts 正確建立 | 加入 BOOST_LOG 輸出 parts 數量 | 數量 = 模型 volume 數量 |
| Step 順序正確 | 觀察 pipeline 執行日誌 | Assembly → Hollowing → DrillHoles |

---

### Phase B：Drill Holes 移植（中風險）

**目標**：把排水孔改走 CSG path，移除 CGAL 3D Boolean。

#### B.1 前置確認

在實作前驗證：
- PhrozenOrca 的 `sla::DrainHole` 結構是否與 `csg::mpartsDrillHoles` 相容
- `model_to_csgmesh()` 的 `mpartsDrillHoles` flag 是否能正確讀取 `m_model_object->sla_drain_holes`

#### B.2 修改 `drill_holes()`

修改檔案：`src/libslic3r/SLAPrintSteps.cpp`

- 加入 `clear_csg(po.m_mesh_to_slice, slaposDrillHoles)`
- 用 `model_to_csgmesh(..., csg::mpartsDrillHoles)` 加入孔洞 CSG parts
- 移除 `hollow_mesh_and_drill()` 呼叫
- 保留 `hollow_mesh_with_holes` 資料結構（Phase C 完成前仍供 `slice_model` 使用）
- 加入 `generate_preview(po, slaposDrillHoles)`

#### B.3 驗證測試

| 測試案例 | 預期結果 |
|---------|---------|
| 無排水孔模型 | 切片結果與移植前完全相同 |
| 1 個排水孔 | 孔洞出現在正確位置，形狀正確 |
| 多個排水孔（>5 個） | 所有孔洞正確，無遺漏 |
| 排水孔幾乎相交 | 不崩潰（原 CGAL 失敗的邊界案例） |
| 排水孔位於薄壁處 | 不崩潰，幾何正確 |
| Hollow disabled + 有排水孔 | 孔洞仍正確顯示（不依賴 interior） |

#### B.4 回歸測試

- 切片層數與移植前相同（允許 ±1 層差異）
- Support points 位置不變（zoffset 相同）
- 無 FDM 相關功能改變

---

### Phase C：Slice Model 移植（中高風險）

**目標**：`slice_model()` 改走 `slice_csgmesh_ex()`，完成 2D CSG 切片路徑。

#### C.1 修改 `hollow_model()`

修改檔案：`src/libslic3r/SLAPrintSteps.cpp`

- 計算 interior mesh 後，加入 `m_mesh_to_slice[slaposHollowing]` 作為 `CSGType::Difference`
- 保留 `m_hollowing_data->interior` 供過渡期使用

#### C.2 修改 `slice_model()`

修改檔案：`src/libslic3r/SLAPrintSteps.cpp`

- 將 `slice_mesh_ex(mesh)` + interior diff 替換為 `slice_csgmesh_ex(range(po.m_mesh_to_slice), slicegrid, params)`
- 確認 `slicegrid`（layer heights、first layer height、Z correction）計算邏輯完全一致

#### C.3 驗證測試

| 測試案例 | 驗證方法 |
|---------|---------|
| 實心模型（無 hollow） | 逐層比對切片輪廓，與舊版差異 < 0.01mm |
| Hollow 模型（無排水孔） | 內腔輪廓正確，壁厚符合設定值 |
| Hollow + 排水孔 | 孔洞在每個切片層正確出現 |
| Hollow + 多個支撐 | 支撐柱的切片輪廓不受影響 |
| Z correction 啟用 | 修正層數與舊版相同 |
| 複雜幾何（含 negative volumes） | 切片不崩潰，結果合理 |
| 薄壁模型（壁厚 < 1mm） | 切片不消失，輪廓連續 |

#### C.4 切片數值驗證方法

```
1. 準備標準測試模型集：
   - benchy_sla.stl（一般模型）
   - hollow_cube.stl（空心盒）
   - drain_test.stl（多個排水孔）
   - thin_wall.stl（薄壁邊界案例）

2. 用移植前 pipeline 產生 reference slices
   （以 ExPolygons 序列化儲存，或截圖每 10 層）

3. 用移植後 pipeline 切片，比對每層輪廓

4. 通過條件：
   - 面積差 < 0.1%
   - 輪廓位移 < 0.01mm
   - 層數差異 ≤ 1 層
```

---

### Phase D：ObjectClipper 整合（低風險）

**目標**：Clip view 改走 CSG range，改善 Hollow Gizmo 渲染效能。

#### D.1 修改 `ObjectClipper::on_update()`

修改檔案：`src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp`、`GLGizmosCommon.hpp`

- SLA 模式改用 `po->get_parts_to_slice()` + `mc->set_mesh(range(partstoslice))`
- 移除 `m_sla_mesh_ptr` cache 欄位（不再需要）
- 保留非 SLA 模式的現有邏輯不變

#### D.2 效能量測基準

在修改前先量測並記錄：

| 量測項目 | 工具 | 記錄方式 |
|---------|------|---------|
| Clip slider 拖動時的 `recalculate_triangles` 耗時 | 加入 `std::chrono` 計時 | ms/frame |
| Hollow gizmo 開啟時的整體 fps | 觀察 fps counter | fps |
| 測試用模型三角形數 | BOOST_LOG 輸出 | 三角形數 |

#### D.3 效能驗證目標

| 測試 | 基準（移植前） | 目標（移植後） |
|------|------------|------------|
| Clip slider 拖動 fps | 量測記錄 | 應有明顯改善（目標 >2×） |
| 鏡頭旋轉 fps | 量測記錄 | 不低於移植前 |
| `recalculate_triangles` 耗時 | 量測記錄 | 應明顯降低 |

#### D.4 視覺驗證

- Clip view 截面填色正確（灰色填充）
- 輪廓線正確（白色邊框）
- Hollow 內腔正確顯示（非填滿）
- 排水孔截面正確顯示

---

### Phase E：Support 介面調整（中風險，可暫緩）

**目標**：`support_points()` 改用 CSG 合併結果的 `indexed_triangle_set`，提升 raycasting 精確度。

**暫緩條件**：若 Phase A–D 完成後 support 功能仍正常，Phase E 可在支撐演算法大改版時一併處理。

#### E.1 調整 `get_mesh_to_print()`

修改檔案：`src/libslic3r/SLAPrint.hpp`、`SLAPrint.cpp`

- 實作回傳 `std::shared_ptr<const indexed_triangle_set>`
- 內部從 CSG parts 以 `generate_preview()` 合併（voxelization 或直接 boolean）

#### E.2 調整 `SupportData` 建構子相容性

修改檔案：`src/libslic3r/SLAPrintSteps.cpp`

- 確認 `SupportData(const indexed_triangle_set&)` 路徑存在
- 若需要，新增相容建構子

#### E.3 驗證測試

| 測試案例 | 驗證方法 |
|---------|---------|
| Auto support 生成 | 支撐點分布合理，覆蓋懸垂區域 |
| Manual support 點 | 點擊位置 raycasting 正確（點落在 mesh 上） |
| Hollow 模型的 support | 內腔不干擾外壁的 raycasting |
| Support tree 生成 | 柱體連接正確，無穿透模型 |

---

## 5. 風險評估

| 風險 | 機率 | 影響 | 緩解策略 |
|------|------|------|---------|
| `slicegrid` Z 計算不一致，導致層數錯誤 | 中 | 高 | Phase C 前先逐行對齊 slicegrid 計算，並在 Phase B 後先量測差異 |
| `model_to_csgmesh()` 不支援 PhrozenOrca 特有的 volume 類型 | 低 | 中 | Phase A.1 完成後立即執行 CSG parts 驗證 |
| `csg::mpartsDrillHoles` 與 PhrozenOrca `DrainHole` 結構不匹配 | 中 | 高 | Phase B.1 前置確認步驟強制執行 |
| Support tree 介面不相容（`TriangleMesh` vs `indexed_triangle_set`） | 中 | 中 | Phase E 獨立，可暫緩至支撐演算法大改版時處理 |
| CSG path 效能比預期差（parts 數量多時） | 低 | 低 | Phase D 有效能量測作為依據，可視情況優化 part 合併策略 |

---

## 6. 不在此次範圍內

- FDM 相關功能（完全不觸及）
- PhrozenOrca 客製化（`BUILD_PHROZEN_ORCA`、PhrozenConnect、PartPlateList 等）
- 支撐演算法本身的修改（SupportTreeBuilder）
- Rasterization pipeline（slaposRasterize）
- Non-Phrozen machine profiles

---

## 7. 完成標準（Definition of Done）

**各 Phase 完成條件**：

- [ ] 所有測試案例通過
- [ ] 無 FDM 功能 regression
- [ ] 無 PhrozenOrca 客製化被修改
- [ ] 新增程式碼符合 PrusaSlicer 命名規範
- [ ] Commit message 格式：`[CSGMesh] Phase X.Y: 描述`

**整體完成條件（Phase A–D）**：

- [ ] Phase A–D 全部完成並通過各自驗證
- [ ] 標準測試模型集切片結果通過數值驗證
- [ ] Clip view 效能量測數據顯示改善
- [ ] 已知 CGAL 失敗邊界案例不再崩潰
- [ ] Phase E 完成，或明確暫緩並在 MEMORY.md 記錄原因

---

## 8. 參考資料

- PrusaSlicer `SLAPrintSteps.cpp` mesh_assembly / hollow_model / drill_holes（行 411–491）
- PrusaSlicer `SLAPrintSteps.cpp` slice_model（行 573–645）
- PrusaSlicer `SliceCSGMesh.hpp`（CSG 2D slice 實作）
- PhrozenOrca `SLAPrintSteps.cpp` drill_holes / slice_model（現有實作）
- PhrozenOrca `GLGizmosCommon.cpp` ObjectClipper::on_update（clip view 現況）
- `CLAUDE.md`：專案開發規則與 Phase 1 完成狀態
