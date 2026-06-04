## 1. Gizmo toolbar — SLA 模式隱藏不適用的 gizmo (sla-ui-feature-gating)

- [x] 1.1 對 `GLGizmoAssembly` 新增 `on_is_selectable()` override，SLA 時回傳 false
- [x] 1.2 對 `GLGizmoBrimEars` 新增 `on_is_selectable()` override，SLA 時回傳 false（同時取消 header 中的宣告註解）
- [x] 1.3 對 `GLGizmoMeshBoolean` 新增 `on_is_selectable()` override，SLA 時回傳 false

## 2. Assembly View toolbar — SLA 模式隱藏 button 與殘留渲染結果 (sla-ui-feature-gating)

- [x] 2.1 在 `_init_assemble_view_toolbar()` 中對 Assembly View toolbar item 設定 `visibility_callback = != ptSLA`
- [x] 2.2 在 `_render_separator_toolbar_right()` 的 `is_enabled()` 檢查後加入 `ptSLA` early return
- [x] 2.3 在 `_render_assemble_view_toolbar()` 的 `is_enabled()` 檢查後加入 `ptSLA` early return
- [x] 2.4 將 `_init_separator_toolbar()` 中 `start_seperator` item 的 dead-code `visibility_callback` 還原為 `return true`

## 3. Right-click menu — SLA 模式 Text disabled、Filament hidden (sla-ui-feature-gating)

- [x] 3.1 將 `GUI_Factories.cpp` 的 `printer_technology()` helper 改為使用 `get_edited_preset()` 而非 `get_selected_preset()`
- [x] 3.2 為 `append_menu_itemm_add_()` 加入可選的 `cb_condition` 與 `parent` 參數，並傳遞給 `append_menu_item()`
- [x] 3.3 在 `append_menu_item_add_text()` 傳入 `cb_condition = printer_technology() != ptSLA` 與 `m_parent`，使 Text 在 SLA 模式於 menu 開啟時動態 disabled
- [x] 3.4 在 `append_menu_item_change_filament()` 加入 `ptSLA` early return，SLA 模式下不加入 filament menu 項目

## 4. 驗證

- [x] 4.1 Build 確認 SLA toolbar 不顯示 Brim Ears、Mesh Boolean、Assembly View slot
- [x] 4.2 確認 SLA 模式下 Assembly View 位置無殘留 separator 線或白色背景矩形
- [x] 4.3 確認 SLA 模式 right-click Add Primitive 中 Text 項目為灰色且不可點擊
- [x] 4.4 確認 SLA 模式 right-click menu 中不出現 Change Filament / Set Filament 項目
- [x] 4.5 確認 FDM 模式 toolbar 與 menu 行為不變（所有項目可見且可用）
- [x] 4.6 檢查所有新增註解，確認 diff 中無 debug / temporary code
