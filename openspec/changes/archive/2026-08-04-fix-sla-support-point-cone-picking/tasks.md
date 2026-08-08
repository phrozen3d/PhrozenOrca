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

- [x] 2.1 確認 `perf-sla-support-points-preview-render` 的 design D2 擺放矩陣（`M_ns · Translation(S · sp.pos) · Rotation(q)`）已實作並通過其驗收 7.6（各 scale 下位置朝向等價）——已於 2026-07-29 archive，`render_points()` 現行的 `model_matrix` 即為此矩陣，`update_point_raycasters_for_picking_transform()` 的 `pick_matrix`/`scaled_pos` 與其定義完全一致（同一份 `instance_matrix`/`instance_scaling_matrix` 建構方式）
- [x] 2.2 順序已滿足，未反序進行
- [x] 2.3 確認 cone picking raycaster 註冊仍在（`register_point_raycasters_for_picking()` 未變動），未被任何後續 change 移除

## 3. Cone raycaster transform

- [x] 3.1 於 `update_point_raycasters_for_picking_transform()` 為 `.second` 計算 transform。**實作時修正**：`width_mm` 不是 robe 實際占據的軸向範圍（只是傳給 `pinhead()` 的名義長度參數），改用 `cone_height = head.fullwidth() - head.r_back_mm`（back 球最寬處、即 `head.junction_point()` 的偏移量，精確值非近似）：`pick_matrix · Translation(scaled_pos + dir · cone_height) · Rotation(q) · Scale(r_back, r_back, cone_height)`（見 design.md D2 修正記錄）
- [x] 3.2 `dir` 使用經 `normal_xform` inverse-transpose 修正後正規化的 `scaled_normal`——透過 `preview_sla_head_for_point(sp, scaled_normal, ...)` 產生 `head.dir`，與 render 路徑用同一份正規化/退化保護邏輯，不重覆手寫
- [x] 3.3 旋轉直接複用 `Quaternion::FromTwoVectors(-UnitZ, head.dir)`，與 render 路徑寫法一致
- [x] 3.4 **cone 高度與實際繪製幾何的對應關係已重新推導**（見 design.md D2）：`get_mesh_preview()` 已於 `fix-sla-support-preview-visual-parity`（2026-08-04 archive）隨死碼移除，`render_points()` 不再有 `preview=true` 分支，manual/auto 點共用同一公式，原提案「manual/auto 分別計算」的疑慮已隨之消解（見 design Open Questions）
- [x] 3.5 呼叫 `m_point_raycasters[i].second->set_transform(cone_matrix)`

## 3a. Back-end sphere raycaster（實作後使用者驗收 5.2 發現遺漏，追加修正）

使用者實測 5.2 時發現：貼在模型表面的 pin 球到 robe 中段都能正確 hover，但**最外側的 back 球點不到**。根因：cone 底面是平面圓盤，只能精確貼合 back 球的赤道（最寬處），過了赤道後球面會彎回收窄到自己的極點，這一段直邊圓錐完全無法涵蓋——而原設計只在 pin 端放了一顆 sphere 補足，back 端什麼都沒放（見 design.md D3 修正記錄）。

- [x] 3a.1 `m_point_raycasters` 型別由 `std::pair<...,...>` 改為具名三欄位 `PointRaycasterSet{pin_sphere, cone, back_sphere}`（`GLGizmoSlaSupports.hpp`）
- [x] 3a.2 `register_point_raycasters_for_picking()` 新增第三個 raycaster 註冊，與前兩者共用同一個 `id = i`（hover 語意不變）、重用同一個共用的 `m_sphere.mesh_raycaster`（不新建 mesh/AABB tree）
- [x] 3a.3 `update_point_raycasters_for_picking_transform()` 為 `back_sphere` 設定 transform：圓心與 cone 底面同一點（`scaled_pos + cone_height · head.dir`），半徑 `r_back_mm`，與可見 back 球完全重合
- [x] 3a.4 `render_points()` 的 clipping 連動 active 管理一併涵蓋 `back_sphere`
- [x] 3a.5 建置確認：0 errors 0 warnings（僅 `GLGizmoSlaSupports.cpp`/`.hpp` 及其反向相依檔案重新編譯）
- [x] 3a.6 效能影響評估（見 design.md D3）：每點 raycaster 數 2→3，皆重用同一份共用 mesh_raycaster，僅 `SceneRaycaster::hit()` 的線性掃描多一個輕量條目；只在滑鼠 hover 判定時執行一次，非逐幀/逐三角形，數百點量級下可忽略

## 4. Active 狀態

- [x] 4.1 `set_active(false)` 的死寫死改為與 `.first`（`pin_sphere`）相同規則：`set_transform()` 後接 `set_active(true)`，不再無條件關閉
- [x] 4.2 確認 `render_points()` 既有的 clipping 連動 active 管理（現涵蓋 `pin_sphere`/`cone`/`back_sphere` 三者，`set_active(!clipped)`）與本處規則一致——本處先開啟，`render_points()` 逐幀依 clipping 覆寫，無互相衝突

## 5. 驗收

- [x] 5.1 點選 pinhead 的 robe 錐體中段可命中該支撐點
- [x] 5.2 點選 pin 球、robe、back 球任一部位皆可命中，全長無 hover gap——重新測試通過：pin 球、robe 中段、back 球三段皆可正確 hover
- [x] 5.3 點選緊鄰視覺輪廓外側的空白處**不**命中（無誤觸）
- [x] 5.4 支撐點密集排列時，命中結果為最近者，符合直覺
- [x] 5.8 Clipping 啟用時被裁切點的 sphere 與 cone 皆為 inactive
- [x] 5.9 Manual 點與 auto 點兩種幾何皆命中正確
- [x] 5.10 `m_hover_id`、拖曳、右鍵刪除行為與修改前一致
- [x] 5.11 Manual Editing 內拖動支撐點後，cone raycaster transform 同步更新至新位置

原 5.5（均勻 scale）/5.6（非均勻 scale）/5.7（鏡像 instance）三項**已移出本 change**，改記到 `fix-sla-support-points-invalidate-on-trafo-change` 的 tasks.md 6.12a/6.12b/6.12c，作為該 change 的驗收項目（見下方 Follow-up）。

## 6. Follow-up（out of scope）

- Points preview 每幀渲染成本 → `perf-sla-support-points-preview-render`
- 若圓錐近似圓台在 pin 端造成可感知的命中差異，評估改用 frustum raycaster 幾何（目前由 pin-end sphere 補足，見 design D3）
- **均勻 scale / 非均勻 scale / 鏡像 instance 下 picking 一致性**（原 5.5/5.6/5.7）→ 移至 `fix-sla-support-points-invalidate-on-trafo-change` tasks.md 6.12a/6.12b/6.12c。原因：物件經 scale/mirror 後既有支撐點再次 apply 會變形，是該 change 要修的缺陷本身；缺陷未修好前，在 scale/mirror 過的物件上測 picking 一致性，測到的落差無法歸因於 picking 邏輯還是被污染的支撐點幾何，量測結果不可靠
