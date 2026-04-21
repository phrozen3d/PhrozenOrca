## 1. Phase A — CSG 基礎架構建立

- [x] 1.1 在 `src/libslic3r/SLAPrint.hpp` 的 `SLAPrintObjectStep` enum 插入 `slaposAssembly`（值 0），其餘步驟值各 +1
- [x] 1.2 在 `SLAPrintObject` class 新增 `std::multiset<CSGPartForStep> m_mesh_to_slice`（PrusaSlicer 實作採 multiset，key 為 step）
- [x] 1.3 在 `SLAPrintObject` class 新增 `std::array<std::shared_ptr<const indexed_triangle_set>, slaposCount+1> m_preview_meshes`
- [x] 1.4 在 `SLAPrint.hpp` 宣告 `get_parts_to_slice()`、`get_parts_to_slice(SLAPrintObjectStep)` 方法
- [x] 1.5 在 `SLAPrintSteps.cpp` 實作 `mesh_assembly(SLAPrintObject&)`：呼叫 `csg::model_to_csgmesh()` 填充 `m_mesh_to_slice[slaposAssembly]`
- [x] 1.6 在 `SLAPrint.cpp` 實作 `get_parts_to_slice()`：以 shallow copy 回傳 multimap range
- [x] 1.7 在 `SLAPrintSteps.cpp` 的 `Steps::execute()` switch-case 加入 `slaposAssembly` → `mesh_assembly()` 分支
- [x] 1.8 同步更新 `OBJ_STEP_LEVELS` 與 `OBJ_STEP_LABELS` 陣列，加入 `slaposAssembly` entry
- [x] 1.9 加入 `csg_inserter` helper struct（參考 PrusaSlicer 實作）
- [x] 1.10 **驗證**：編譯通過；開啟 SLA 模型執行切片，確認 `slaposAssembly` 執行，日誌輸出 CSG parts 數量正確

## 2. Phase B — Drill Holes CSG 雙軌並行

> **策略修訂（D4 修訂後）**：保留 CGAL 路徑確保切片正確，同時注冊 CSG parts 供 Phase C/D 使用。
> 原計畫直接移除 CGAL 的兩次嘗試均導致孔洞消失（見 design.md Implementation Findings）。

- [x] 2.1 **前置確認（已完成）**：`mpartsDrillHoles` 在 PhrozenOrca **不可用**；
      `ModelToCSGMesh.hpp` drain holes 區段已 comment out（依賴 PrusaSlicer 專屬 free function）；
      改為手動迭代 `po.transformed_drainhole_points()` + `dhole.to_mesh()`
- [x] 2.2 在 `drill_holes()` 開頭加入 `clear_csg(po.m_mesh_to_slice, slaposDrillHoles)`
- [x] 2.3 在 CGAL Boolean 執行結束後，遍歷 `po.transformed_drainhole_points()`，
      以 `csg_inserter{po.m_mesh_to_slice, slaposDrillHoles}` 逐一加入 CSG Difference parts
      （**不移除** CGAL 程式碼，兩路並行）
- [x] 2.4 加入 `generate_preview(po, slaposDrillHoles)` 呼叫（含所有 early return 路徑）
- [x] 2.5 **驗證（回歸）**：切片結果與修改前**完全相同**（CGAL 路徑不動，層數/輪廓應 100% 一致）
- [x] 2.6 **驗證（CSG parts 數量）**：log 確認 `m_mesh_to_slice` 中 slaposDrillHoles entries
      數量正確（0 個排水孔 → 0 個 entry；N 個排水孔 → N 個 entry）
- [x] 2.7 **驗證（多個排水孔 > 5）**：確認所有排水孔 CSGPart 加入，無遺漏
- [x] 2.8 **驗證（邊界案例）**：兩個幾乎相交的排水孔模型，確認不崩潰

## 3. Phase C — Slice Model 移植

> **前置條件（Gate）**：3.0 雙軌驗證通過後，才能執行 3.3（切換 slice 路徑）。
> 3.3 之前 CGAL 路徑與 CSG 路徑並行，確認等價後再移除舊路徑。

- [x] 3.0 **雙軌驗證（新增 Gate）**：在 `slice_model()` 中**暫時同時執行**兩條路徑
      （`slice_mesh_ex(hollow_mesh_with_holes)` 舊路徑 + `slice_csgmesh_ex(m_mesh_to_slice)` 新路徑），
      逐層比對輸出輪廓，確認面積差 < 0.1%、位移 < 0.01mm；
      **特別檢查**：drain hole 圓柱（world-space mesh + Identity trafo）與
      model parts（local-space mesh + instance trafo）在同一 Z grid 的對齊正確性；
      **Gate 通過後才繼續 3.3**
      *(通過：實心、hollow 無孔、hollow + 排水孔 三類測試均 PASS)*
- [x] 3.1 在 `hollow_model()` 中，interior mesh 計算完成後加入 `m_mesh_to_slice[slaposHollowing]` 作為 `CSGType::Difference` part
      （swap_normals 後存入，確保 slice_csgmesh_ex 產生 solid cross-section 供 diff_ex 使用）
- [x] 3.2 逐行比對 `slice_model()` 的 slicegrid 計算（first layer height、layer height、Z correction offset）與舊路徑一致性，記錄差異
      （Gate 驗證使用相同 slicegrid，Z range 一致）
- [x] 3.3 **（需 3.0 Gate 通過）** 在 `slice_model()` 中以 `slice_csgmesh_ex(range(po.m_mesh_to_slice), slicegrid, params)` 取代 `slice_mesh_ex(mesh, slicegrid, params)`
- [x] 3.4 移除 `slice_model()` 中的 interior 額外 diff 步驟（舊的 `diff_ex(m_model_slices[i], interior_slices[i])`）
- [x] 3.5 **驗證（實心模型）**：用 benchy_sla.stl，逐層比對新舊路徑輪廓，面積差 < 0.1%，位移 < 0.01mm
- [x] 3.6 **驗證（hollow 無排水孔）**：確認 hollow_cube.stl 內腔輪廓正確，壁厚符合設定
- [x] 3.7 **驗證（hollow + 排水孔）**：確認孔洞在每個切片層正確出現
- [x] 3.8 **驗證（Z correction）**：N/A — `zcorrection_layers` 無 UI 入口，所有 preset 預設值為 0（停用），`apply_zcorrection` 為 no-op，與切片路徑無關
- [x] 3.9 **驗證（薄壁）**：確認薄壁模型切片不消失，輪廓連續
- [x] 3.10 移除對 `hollow_mesh_with_holes` 的切片依賴（確認 `slice_model()` 不再讀取該欄位）

## 4. Phase D — ObjectClipper 整合

- [ ] 4.1 **量測基準**：在修改前加入計時碼，記錄現有 `recalculate_triangles()` 耗時（ms/frame）與 clip slider 拖動 fps
- [ ] 4.2 在 `GLGizmosCommon.cpp` 的 `ObjectClipper::on_update()` SLA 分支中，以 `po->get_parts_to_slice()` 取得 CSG parts
- [ ] 4.3 以 `mc->set_mesh(range(partstoslice))` 取代 `sla_mc->set_mesh(new_sla_mesh_ptr->its)`
- [ ] 4.4 移除 `GLGizmosCommon.hpp` 中的 `m_sla_mesh_ptr` 成員變數
- [ ] 4.5 移除 `on_release()` 與 `on_update()` 中對 `m_sla_mesh_ptr` 的所有引用
- [ ] 4.6 **驗證（視覺正確）**：截面灰色填滿、輪廓線白色、hollow 內腔空洞、排水孔缺口均正確顯示
- [ ] 4.7 **驗證（效能）**：量測新路徑的 `recalculate_triangles()` 耗時，確認相比 Phase D.1 基準有明顯改善

## 5. Phase E — Support 介面調整（可暫緩）

- [ ] 5.1 **評估條件**：Phase A–D 完成後，測試 auto support 與 manual support raycasting；若正常則暫緩 Phase E
- [ ] 5.2 實作 `get_mesh_to_print()`：從 CSG parts 以 boolean 合併（或 VDB voxelization）產生 `std::shared_ptr<const indexed_triangle_set>`
- [ ] 5.3 確認 `SupportData` 建構子接受 `const indexed_triangle_set&`；若需要，新增相容建構子
- [ ] 5.4 修改 `support_points()` 中的 `SupportData` 初始化，改用 `get_mesh_to_print()` 回傳值
- [ ] 5.5 **驗證（auto support）**：支撐點分布合理，覆蓋懸垂區域
- [ ] 5.6 **驗證（manual support raycasting）**：點擊位置正確落在 mesh 表面，hollow 內腔不干擾

## 6. 整合驗證與收尾

- [ ] 6.1 執行標準測試模型集完整切片流程（benchy_sla + hollow_cube + drain_test），確認所有結果通過數值驗證
- [ ] 6.2 確認 FDM 模式切片流程不受影響（選擇 FDM printer，執行完整切片）
- [ ] 6.3 確認 PhrozenOrca 客製化功能正常（PartPlateList、PhrozenConnect 介面等）
- [ ] 6.4 移除所有過渡期保留的舊路徑程式碼（`hollow_mesh_with_holes` 等不再需要的欄位）
- [ ] 6.5 更新 `MEMORY.md`，記錄 CSGMesh 架構移植完成狀態與 Phase E 暫緩決定
