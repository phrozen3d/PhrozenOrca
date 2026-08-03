## 0. 前置條件與實施順序

- 本 change **無前置**，與 `fix-sla-support-point-issues` 分支既有項目互相獨立

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 根因確認（已於 explore 討論階段完成）

- [x] 1.1 確認 `process_top_float_live()`（`GLGizmoSlaSupports.cpp:130-158`）與 `process_contact_type_is_sphere()`（`:102-126`）皆優先讀 `Tab::get_field(key)->get_value()`——widget 目前顯示的文字，欄位不存在才退回 `sla_process_config()`
- [x] 1.2 確認 `read_preview_top_params_live()`（`:237-247`）每幀呼叫一次上述兩函式，組成 `PreviewTopParams`，套用給所有 `use_stored_geometry == false` 的點（全部沒有 explicit geometry 的點）
- [x] 1.3 確認 `begin_support_point_top_field_display()`（`Tab.cpp`，`fix-sla-support-top-config-enum-set` 建立）選定支撐點時會把同一組 widget 的顯示文字**暫時改寫**成該點的值
- [x] 1.4 確認上述兩讀值函式**無法區分**現在 widget 顯示的是「Process tab 真正即時值」還是「借用來顯示某點的值」
- [x] 1.5 確認 `has_selected_support_points()`（`GLGizmoSlaSupports.hpp:115`，`return m_editing_mode && !m_selection_empty`）是判斷「widget 是否正被借用」的既有、準確條件，與觸發 `begin_support_point_top_field_display()` 的條件相同
- [x] 1.6 等價性論證（design.md D2）：確認選中點自身的 preview 不吃 `top_params`（`preview_use_stored_top()` 對 `point_selected==true` early return，`use_stored_point` 恆為 true），本 change 對選中點的顯示零影響，只影響其餘吃 `top_params` 的點

## 2. 修正

- [ ] 2.1 `process_top_float_live()` 開頭新增守門：透過 `GLGizmoSlaSupports::active_instance()` 取得 gizmo，若 `has_selected_support_points() == true`，直接讀 `sla_process_config()` 並 return，不觸碰 widget
- [ ] 2.2 `process_contact_type_is_sphere()` 套用同樣的守門邏輯
- [ ] 2.3 確認兩處新增的守門邏輯在 `active_instance()` 回傳 nullptr 時安全降級為原本的 widget 讀值路徑（不是新的失敗模式）
- [ ] 2.4 確認未新增第二套「該讀哪裡」的判斷條件——兩處都直接沿用 `has_selected_support_points()`，不新造旗標

## 3. 驗證：污染消除

- [ ] 3.1 Points 檢視含多顆 auto 點與一顆有 explicit geometry 的 manual 點；選中 manual 點並編輯 `support_head_front_diameter`，確認 auto 點的錐體直徑全程不變
- [ ] 3.2 Points 檢視含多顆 auto 點；選中其中一顆 auto 點並編輯其 `support_head_front_diameter`，確認**其他** auto 點的錐體不受影響，只有被選中的那顆自己的 preview 反映編輯（透過它自己的 stored geometry，不透過本次修正）
- [ ] 3.3 選中一顆點後切換其 `support_contact_type`（None ↔ Sphere），確認其他 auto 點的 contact sphere 顯示在整個選取期間不受影響
- [ ] 3.4 完成 3.1 的操作後取消選取，確認 auto 點的外觀從頭到尾沒有變化過（因為從未被污染，不是「變了又變回來」）

## 4. 驗證：不得回歸

- [ ] 4.1 **live 打字語意保留**：無任何點被選取時，於 Process tab 編輯 `support_head_front_diameter`、尚未失焦，確認下一次重繪 auto 點的錐體直徑跟著變——與修改前行為一致
- [ ] 4.2 確認 `default_contact_sphere_radius_mm()` 等其餘呼叫 `sla_process_config()` 的既有路徑不受本 change 影響
- [ ] 4.3 確認 `fix-sla-support-preview-stored-geometry-in-auto-mode` 已驗收過的情境（manual 點在非編輯模式維持自身尺寸）不受本 change 影響
- [ ] 4.4 確認 `fix-sla-support-top-config-enum-set` 已驗收過的 per-point Top 欄位顯示／回填／取消選取還原 preset 等行為不受本 change 影響
- [ ] 4.5 切片輸出與修改前完全相同（本 change 只影響 preview 讀值來源，不影響切片）

## 5. Follow-up（out of scope）

- auto 點編輯後 `type` 是否該轉換為 `manual_add` → 併入 `fix-sla-support-preview-geometry-source-semantics` 的決策範圍
- 支撐點的幾何尺寸解析規則（per-point vs preset 的判定邏輯）→ `fix-sla-support-preview-geometry-source-semantics`
- Points preview 的形狀/光影/顏色統一 → `fix-sla-support-preview-visual-parity`
