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

- [x] 2.1 `process_top_float_live()` 開頭新增守門：透過 `GLGizmoSlaSupports::active_instance()` 取得 gizmo，若 `has_selected_support_points() == true`，跳過 widget 讀值區塊，直接落到既有的 `sla_process_config()` fallback（未沿用 design.md 草稿裡另外重寫一份 fallback 的寫法，避免與函式尾端既有邏輯重複——見下方實作備註）
- [x] 2.2 `process_contact_type_is_sphere()` 套用同樣的守門邏輯，同樣落到既有的 fallback 區塊（`cfg.option` 再 `preset.option`），不重寫
- [x] 2.3 確認兩處新增的守門邏輯在 `active_instance()` 回傳 nullptr 時安全降級為原本的 widget 讀值路徑（不是新的失敗模式）——`gizmo && gizmo->has_selected_support_points()` 的短路寫法確保 nullptr 時 `widget_borrowed_by_selection` 為 false，等同未修改前的行為
- [x] 2.4 確認未新增第二套「該讀哪裡」的判斷條件——兩處都直接沿用 `has_selected_support_points()`，不新造旗標

**實作備註**：design.md D1 的範例程式碼在守門分支內重新手寫了一份 `sla_process_config()` fallback（含 `cfg.has(key)`／`opt_float` 或 `cfg.option`／`preset.option` 的完整邏輯）。實作時改為讓守門分支只負責「跳過 widget 讀值」，直接落入函式尾端**既有**的 fallback 程式碼——兩個函式尾端本來就有一份寫好、且已在「欄位不存在」情況下驗證過的 fallback，沒有理由為「被選取」這個新情境另外複製一份幾乎相同的邏輯。行為與 design.md 的意圖完全一致（有選取時一律用 `sla_process_config()`），只是少一份重複程式碼。

## 3. 驗證：污染消除

- [x] 3.1 Points 檢視含多顆 auto 點與一顆有 explicit geometry 的 manual 點；選中 manual 點並編輯 `support_head_front_diameter`，確認 auto 點的錐體直徑全程不變
- [x] 3.2 Points 檢視含多顆 auto 點；選中其中一顆 auto 點並編輯其 `support_head_front_diameter`，確認**其他** auto 點的錐體不受影響，只有被選中的那顆自己的 preview 反映編輯（透過它自己的 stored geometry，不透過本次修正）
- [x] 3.3 選中一顆點後切換其 `support_contact_type`（None ↔ Sphere），確認其他 auto 點的 contact sphere 顯示在整個選取期間不受影響——**通過，其他點未被污染**，本 change 的修正生效。另實測發現一個相關但不同範疇的既有 bug：切換後選取期間顯示正確，但**取消選取後該點自己**變回球體外觀（不是「其他點」被污染，是被編輯的那顆點自己沒能保留編輯結果）。追查後確認是 `fix-sla-support-preview-geometry-source-semantics` D2a 記載的既有問題（`apply_process_top_option()` 不會把 auto 點的 `sp.type` 轉成 `manual_add`，取消選取後 `has_explicit_geometry()` 卡住）透過 `support_contact_type` 欄位再次驗證到，非本 change 引入的回歸，已補充記錄於 D2a（見其「第二次確認」段落），不影響本項通過判定
- [x] 3.4 完成 3.1 的操作後取消選取，確認 auto 點的外觀從頭到尾沒有變化過（因為從未被污染，不是「變了又變回來」）

## 4. 驗證：不得回歸

- [x] 4.1 **live 打字語意保留**：無任何點被選取時，於 Process tab 編輯 `support_head_front_diameter`、尚未失焦，確認下一次重繪 auto 點的錐體直徑跟著變——與修改前行為一致
- [x] 4.2 確認 `default_contact_sphere_radius_mm()` 等其餘呼叫 `sla_process_config()` 的既有路徑不受本 change 影響。**經程式碼審查確認，非執行期量測**：`grep sla_process_config()` 全部 8 個呼叫點中，只有 `process_top_float_live()`／`process_contact_type_is_sphere()` 這兩處被本 change 修改（新增進入前的守門判斷）；其餘 6 處（`support_top_apply_point()`、`support_top_config_from_selection()`、`m_base_diameter_before_change` 的兩處基準讀取、`unsaved_changes()`）皆在本次 diff 之外，逐一核對呼叫端程式碼確認未被觸碰
- [x] 4.3 確認 `fix-sla-support-preview-stored-geometry-in-auto-mode` 已驗收過的情境（manual 點在非編輯模式維持自身尺寸）不受本 change 影響。**經程式碼審查確認**：`has_selected_support_points()`（`GLGizmoSlaSupports.hpp:115`）定義為 `m_editing_mode && !m_selection_empty`，非編輯模式下 `m_editing_mode` 恆為 false，本 change 新增的守門邏輯因此在非編輯模式下必然不生效——結構上保證不影響該情境
- [x] 4.4 確認 `fix-sla-support-top-config-enum-set` 已驗收過的 per-point Top 欄位顯示／回填／取消選取還原 preset 等行為不受本 change 影響。**經程式碼審查確認**：`begin_support_point_top_field_display()`／`end_support_point_top_field_display()` 定義於 `Tab.cpp`，本 change 的完整 diff 只觸及 `GLGizmoSlaSupports.cpp`（`git diff --stat` 確認 `Tab.cpp`／`OptionsGroup.cpp` 零改動），widget 本身的顯示/回填機制未被觸碰，只改變其他函式「讀」這些 widget 的時機
- [x] 4.5 切片輸出與修改前完全相同（本 change 只影響 preview 讀值來源，不影響切片）

## 5. Follow-up（out of scope）

- auto 點編輯後 `type` 是否該轉換為 `manual_add` → 併入 `fix-sla-support-preview-geometry-source-semantics` 的決策範圍
- 支撐點的幾何尺寸解析規則（per-point vs preset 的判定邏輯）→ `fix-sla-support-preview-geometry-source-semantics`
- Points preview 的形狀/光影/顏色統一 → `fix-sla-support-preview-visual-parity`
