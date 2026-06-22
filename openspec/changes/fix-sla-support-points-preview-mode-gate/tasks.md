## 1. 根因確認

- [x] 1.1 `GLGizmoSlaSupports::on_render()` 第 558 行無條件呼叫 `render_points(selection)`，未檢查 `m_show_support_structure`
- [x] 1.2 `render_points()` 本身未檢查 `m_show_support_structure`
- [x] 1.3 View-mode 切換 callback 未呼叫 `unregister_point_raycasters_for_picking()` 也未 `set_as_dirty()`
- [x] 1.4 `activate_structure_view()` 由 `GLGizmoLcdOverhangDetection` 呼叫，同樣未處理 Points preview 殘留
- [x] 1.5 `sla::SupportPoint::pos` 在 raw model-object 座標；可見 mesh 由 `update_volumes()` 套用完整 instance transform（含 scale）。原始 render / picking 公式 `instance_matrix * instance_scaling_matrix_inverse` 移除了全部 scale，造成 cone 落在未縮放位置
- [x] 1.6 改成保留 scale 後位置正確，但 cone 直徑 / 長度也隨 scale 變大／變小，且 picking sphere 半徑同步被拉大

## 2. Render mode gate

- [x] 2.1 在 `render_points()` 入口加入 `if (m_show_support_structure && !m_editing_mode) return;`
- [x] 2.2 確認 editing mode 路徑（`m_editing_mode == true`）不受影響，仍能渲染 `m_editing_cache` 的 cone

## 3. View button callback 同步

- [x] 3.1 Structure 按鈕 callback：`if (!m_editing_mode) unregister_point_raycasters_for_picking();`
- [x] 3.2 Structure 按鈕 callback 結尾呼叫 `m_parent.set_as_dirty();`
- [x] 3.3 Points 按鈕 callback：呼叫 `register_point_raycasters_for_picking();`（內部已有 editing-mode + non-empty cache guard，外部 mode 下 no-op）
- [x] 3.4 Points 按鈕 callback 結尾呼叫 `m_parent.set_as_dirty();`

## 4. activate_structure_view() 同步

- [x] 4.1 mirror 3.1：`if (!m_editing_mode) unregister_point_raycasters_for_picking();`
- [x] 4.2 mirror 3.2：結尾呼叫 `m_parent.set_as_dirty();`

## 5. Position / size split

- [x] 5.1 `render_points()` 改成：`head.pos = instance_scaling_matrix * support_point.pos.cast<double>()`，覆寫 `preview_sla_head_for_point` 建構時填入的 raw pos
- [x] 5.2 `render_points()` 的 `model_matrix = instance_matrix * instance_scaling_matrix_inverse`（移除 positive scale；mirror 透過 signed `get_matrix()` vs unsigned `get_scaling_factor_matrix()` 保留；front face 翻轉沿用 `vol->is_left_handed()`）
- [x] 5.3 normal 用 inverse-transpose `S` 轉成 scaled-mesh surface normal；uniform scale 等價於原行為
- [x] 5.4 picking：sphere 中心 = `instance_scaling_matrix * sp.pos`、`pick_matrix = instance_matrix * instance_scaling_matrix_inverse`、半徑 = `max(head.r_pin_mm, head.r_contact_mm)`
- [x] 5.5 確認 render 與 picking 使用同一套拆解，hover / click 與視覺一致

## 6. Follow-up（out of scope，不在本 change 驗收項目內）

- Structure mode undo/redo async reslice 不刷新（[KB-2]） → 候選 follow-up `fix-sla-supports-structure-view-undo-refresh`
- Points preview 效能（normal cache / geometry cache / per-point GL buffer churn）→ 另一輪 follow-up，本 change 不處理
- `get_data_from_backend()` 內 `po->trafo().inverse()` 的座標系行為作為本 change 的前置假設，已透過閱讀驗證；未修改該函式

## 7. 建議手動驗證流程（不列入本 change 完成條件，供 QA / regression 參考）

### Mode gate 回歸

- 載入 SLA preset → 載入有支撐需求的模型 → 進入 SLA Support gizmo
- Auto-generate 支撐點 → 在非編輯模式下切到 Structure → 確認 Points cone 完全消失，畫面只有 support tree + pad mesh
- 同上場景，hover 在原本 cone 的位置 → 確認沒有任何 hover highlight，picking 落到 Structure mesh
- 切回 Points → 確認 cone 重新出現於原位置
- 進入 Manual Editing → 切到 Structure → 確認 cone 仍然顯示且可選/拖（editing-mode override 生效）
- 離開 Manual Editing → 確認 view mode 回復後 cone 顯示正確
- 由 Overhang Detection 呼叫 `activate_structure_view()` → 確認沒有殘留 cone、無 hover ghost
- 在 Structure 模式時切換 Auto / Manual tab → 確認 cone 不會閃現

### Position / size 對齊

- Uniform scale 1.5：cone anchor 貼合縮放後模型表面；cone 直徑 / 長度與 scale=1 時相同
- Uniform scale 2.0：同上
- Uniform scale 0.5：同上，cone 不縮小
- Non-uniform scale `(2, 1, 1)`：cone anchor 落在 X 拉伸後表面；cone 截面仍為圓形；cone 軸沿縮放後可見表面法向（不沿 raw 法向）
- Non-uniform scale `(1, 1, 3)`：cone 長度不變；垂直表面 cone 沿世界 ±Z
- Lift（Model Lift Height）+ scale 疊加：cone 位置 / 大小皆正確
- Mirror（is_left_handed）：cone 不倒裝、front face 不黑

### Picking 一致性

- 各種 scale 下 hover cone 視覺邊緣 → 立刻 hover；hover cone 旁空白 → 不 hover
- Manual Editing 內拖動 cone → 拖動後 cone 重畫在新位置、且 picking sphere 同步
- Auto-generate 後 hover：picking 半徑為 mm（與 scale 無關）
