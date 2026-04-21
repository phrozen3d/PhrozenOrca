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

## 2. Phase B — Drill Holes 移植

- [ ] 2.1 **前置確認**：閱讀 `libslic3r/CSGMesh/ModelToCSGMesh.hpp`，確認 `mpartsDrillHoles` flag 能讀取 `sla_drain_holes`；若不相容，記錄需要的適配工作
- [ ] 2.2 在 `SLAPrintSteps.cpp` 的 `drill_holes()` 開頭加入 `clear_csg(po.m_mesh_to_slice, slaposDrillHoles)`
- [ ] 2.3 以 `csg::model_to_csgmesh(*po.model_object(), po.trafo(), csg_inserter{po.m_mesh_to_slice, slaposDrillHoles}, csg::mpartsDrillHoles)` 加入排水孔 CSG parts
- [ ] 2.4 移除 `hollow_mesh_and_drill()` 的 CGAL 3D Boolean 呼叫（保留 `hollow_mesh_with_holes` 欄位供 Phase C 前過渡使用）
- [ ] 2.5 加入 `generate_preview(po, slaposDrillHoles)` 呼叫
- [ ] 2.6 **驗證（無排水孔）**：切片結果層數與修改前相同（允許差 ≤ 1 層）
- [ ] 2.7 **驗證（單個排水孔）**：確認排水孔 CSGPart 正確加入，孔洞在預覽中位置正確
- [ ] 2.8 **驗證（多個排水孔 > 5）**：確認所有排水孔 CSGPart 加入，無遺漏
- [ ] 2.9 **驗證（邊界案例）**：使用兩個幾乎相交的排水孔模型，確認不崩潰

## 3. Phase C — Slice Model 移植

- [ ] 3.1 在 `hollow_model()` 中，interior mesh 計算完成後加入 `m_mesh_to_slice[slaposHollowing]` 作為 `CSGType::Difference` part
- [ ] 3.2 逐行比對 `slice_model()` 的 slicegrid 計算（first layer height、layer height、Z correction offset）與舊路徑一致性，記錄差異
- [ ] 3.3 在 `slice_model()` 中以 `slice_csgmesh_ex(range(po.m_mesh_to_slice), slicegrid, params)` 取代 `slice_mesh_ex(mesh, slicegrid, params)`
- [ ] 3.4 移除 `slice_model()` 中的 interior 額外 diff 步驟（舊的 `diff_ex(m_model_slices[i], interior_slices[i])`）
- [ ] 3.5 **驗證（實心模型）**：用 benchy_sla.stl，逐層比對新舊路徑輪廓，面積差 < 0.1%，位移 < 0.01mm
- [ ] 3.6 **驗證（hollow 無排水孔）**：確認 hollow_cube.stl 內腔輪廓正確，壁厚符合設定
- [ ] 3.7 **驗證（hollow + 排水孔）**：確認孔洞在每個切片層正確出現
- [ ] 3.8 **驗證（Z correction）**：啟用 Z correction，確認修正層數與舊路徑相同
- [ ] 3.9 **驗證（薄壁）**：確認薄壁模型切片不消失，輪廓連續
- [ ] 3.10 移除對 `hollow_mesh_with_holes` 的切片依賴（確認 `slice_model()` 不再讀取該欄位）

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
