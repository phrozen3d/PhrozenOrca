## 0. 前置條件

- 相依的 `perf-sla-support-points-preview-render` 已於 2026-07-29 archive，其 design D2 的擺放矩陣 `M_ns · Translation(S · sp.pos) · Rotation(q)` 已實作並通過驗收 7.6。**前置已滿足，可立即進行**（第 2 節僅需覆核）

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 根因確認（已於提案階段完成）

- [x] 1.1 確認命中判定僅由 sphere raycaster 承擔：`update_point_raycasters_for_picking_transform()`（`:2397-2417`）只對 `.first` 呼叫 `set_transform()`（`:2415`），`.second` 始終停留在註冊時的 `Transform3d::Identity()`（`:2364`）
- [x] 1.2 確認 `.second` 永遠停用：`:2412` 寫死 `set_active(false)`，無任何路徑會啟用它
- [x] 1.3 確認有效命中半徑為 `max(head.r_pin_mm, head.r_contact_mm)`（`:2409`）；預設 `support_head_front_diameter = 0.4 mm` 時僅 0.2 mm，而視覺體長約 `width_mm = 2 mm`
- [x] 1.4 確認兩個 raycaster 以**同一個 `id = i`** 註冊（`:2363-2364`），故命中任一皆回報相同 `m_hover_id`，本修復不需調整 hover id 語意
- [x] 1.5 確認單位錐幾何約定（`TriangleMesh.cpp:1063-1087`）：底面圓心在原點、位於 `z = 0` 平面、半徑 1，尖端在 `(0, 0, h)`，軸向 `+Z`
- [x] 1.6 確認 `render_points()`（`:651-653`）已對 `.first` 與 `.second` 同時做 clipping 連動的 active 管理，該處無需修改
- [x] 1.7 確認此缺陷非由 `fix-sla-support-points-preview-mode-gate` 引入，但其建立的位置／尺寸拆解為本修復基礎

## 2. 前置：確認相依 change 狀態

- [ ] 2.1 確認 `perf-sla-support-points-preview-render` 的 design D2 擺放矩陣（`M_ns · Translation(S · sp.pos) · Rotation(q)`）已實作並通過其驗收 7.6（各 scale 下位置朝向等價）
- [ ] 2.2 若該 change 尚未實作，評估是否改為反序進行；反序時需自行推導旋轉並於該 change 實作後重新驗證兩邊一致（見 design D5）
- [ ] 2.3 確認該 change 未移除 cone picking raycaster（其 proposal Non-goals 與 design D7 已明令禁止）

## 3. Cone raycaster transform

- [ ] 3.1 於 `update_point_raycasters_for_picking_transform()` 為 `.second` 計算 transform：`pick_matrix · Translation(scaled_pos + dir · width) · Rotation(q) · Scale(r_back, r_back, width)`
- [ ] 3.2 `dir` 使用經 `normal_xform` inverse-transpose 修正後正規化的 `scaled_normal`，與 render 路徑一致
- [ ] 3.3 旋轉直接複用 render 路徑的 `q = FromTwoVectors(-UnitZ, dir)`；確認其對單位錐 `+Z` 的作用等同 `FromTwoVectors(UnitZ, -dir)`（錐體旋轉對稱，roll 無影響）
- [ ] 3.4 **確認 cone 高度與實際繪製幾何一致**：`head_mesh_local()` 在 preview 模式下軸向長度為 `width_mm - 2·r_pin - 2·r_back`（`SupportTreeMesher.hpp:53-55`），與非 preview 模式不同；依 `manual_add`（`get_mesh_preview`）與 auto（`get_mesh`）分別取值或確認單一公式可涵蓋兩者（見 design Open Questions）
- [ ] 3.5 呼叫 `m_point_raycasters[i].second->set_transform(cone_matrix)`

## 4. Active 狀態

- [ ] 4.1 `:2412` 的 `set_active(false)` 改為與 `.first` 相同的 clipping 連動規則
- [ ] 4.2 確認 `render_points()`（`:651-653`）既有的 active 管理與本處規則一致，無互相覆寫

## 5. 驗收

- [ ] 5.1 點選 pinhead 的 robe 錐體中段可命中該支撐點
- [ ] 5.2 點選 pin 球、robe、back 球任一部位皆可命中，全長無 hover gap
- [ ] 5.3 點選緊鄰視覺輪廓外側的空白處**不**命中（無誤觸）
- [ ] 5.4 支撐點密集排列時，命中結果為最近者，符合直覺
- [ ] 5.5 均勻 scale 1.5 / 2.0 / 0.5 下 picking 與視覺一致
- [ ] 5.6 非均勻 scale `(2,1,1)` 與 `(1,1,3)` 下 cone raycaster 軸向跟隨縮放後表面法向，與視覺一致
- [ ] 5.7 鏡像 instance（`is_left_handed()` 為 true）下錐體命中方向正確、未反轉
- [ ] 5.8 Clipping 啟用時被裁切點的 sphere 與 cone 皆為 inactive
- [ ] 5.9 Manual 點（`get_mesh_preview`）與 auto 點（`get_mesh`）兩種幾何皆命中正確
- [ ] 5.10 `m_hover_id`、拖曳、右鍵刪除行為與修改前一致
- [ ] 5.11 Manual Editing 內拖動支撐點後，cone raycaster transform 同步更新至新位置

## 6. Follow-up（out of scope）

- Points preview 每幀渲染成本 → `perf-sla-support-points-preview-render`
- 若圓錐近似圓台在 pin 端造成可感知的命中差異，評估改用 frustum raycaster 幾何（目前由 sphere 補足，見 design D3）
