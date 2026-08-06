## 0. 前置條件與實施順序

- 本 change **無前置**，是 `fix-sla-support-top-params-live-read-isolation`（2026-08-04 archive）的直接延伸，與 `fix-sla-support-point-issues` 分支其他 change 互相獨立
- 與仍在決策階段的 `fix-sla-support-preview-geometry-source-semantics` 不同軸線（那邊決定「該用哪個幾何來源」，本 change 修「現有規則的守門條件跟它要追蹤的狀態偶爾不同步」的時序 bug），不需要等其 Q1/Q2/Q3 決策落地

> 全域相依圖與實施順序見 [`openspec/changes/README.md`](../README.md)。

## 1. 根因確認（已於提案階段完成）

- [x] 1.1 確認 `has_selected_support_points()`（`GLGizmoSlaSupports.hpp:115`）在支撐點選取/取消選取當下**同步**變化
- [x] 1.2 確認 widget 實際顯示內容的還原/借用，由 `notify_process_tab_selection_changed()`（`GLGizmoSlaSupports.cpp:1949`）觸發，但實際呼叫 `begin_support_point_top_field_display()` / `end_support_point_top_field_display()` 包在 `wxTheApp->CallAfter(...)` 裡，延後到下一個事件迴圈才執行
- [x] 1.3 確認取消選取瞬間到 `CallAfter` 真正執行之間存在時序空窗：`has_selected_support_points()` 已變 false，但 widget 仍顯示剛剛取消選取那顆點的借用值
- [x] 1.4 確認 `process_top_float_live()` / `process_contact_type_is_sphere()`（`fix-sla-support-top-params-live-read-isolation` 建立的守門邏輯）在此空窗期會誤判「沒有選取」而放行讀 widget，讀到尚未還原的舊值，套用到其他未選取的點
- [x] 1.5 確認此問題僅發生於「取消選取」方向：選取方向 `has_selected_support_points()` 立刻變 true，守門邏輯立刻生效，不受 widget 是否已跟上影響
- [x] 1.6 確認 `TabSLAPrint::m_support_point_top_field_active`（`Tab.hpp:687`）是現成、更精確的「widget 是否仍在借用中」訊號，其生命週期精確對應 `begin_...` 到 `end_...` 真正執行之間，目前為私有成員、無 public getter

## 2. 實作

- [x] 2.1 `Tab.hpp` 新增 `TabSLAPrint::is_support_point_top_field_active() const`，比照既有 `is_support_point_top_field_update()` 的寫法（見 design.md D1）
- [x] 2.2 `process_top_float_live()` 的守門條件改為聯集：`has_selected_support_points() || tab->is_support_point_top_field_active()`（見 design.md D2，注意是 OR 不是替換）
- [x] 2.3 `process_contact_type_is_sphere()` 套用同樣的聯集守門邏輯
- [x] 2.4 確認兩處新增的 `is_support_point_top_field_active()` 呼叫在 `tab` 為 nullptr 時安全降級（沿用既有的 `tab &&` 防禦寫法）
- [x] 2.5 建置確認：0 errors 0 warnings

## 3. 驗證：取消選取瞬間不閃現

- [x] 3.1 Points 檢視含多顆 auto 點；選中一顆 auto 點編輯 `support_head_front_diameter`，取消選取（點擊 3D 視窗空白處），確認其他 auto 點的錐體**全程**不變化，沒有任何一幀短暫變成剛編輯的值——**本項通過，其他點全程未受影響**，本 change 的聯集守門修正生效。使用者同時觀察到：**被編輯的那顆點自己**取消選取後跳回未編輯外觀、顏色也沒變成手動編輯後該有的樣子——這不是「其他點」被連動，是另一個既有問題（`fix-sla-support-preview-geometry-source-semantics` D2a：`apply_process_top_option()` 編輯 auto 點時不轉換 `sp.type`，取消選取後 `has_explicit_geometry()` 卡住），已記入該 change 的「第三次確認」，非本 change 範圍、非回歸
- [x] 3.2 同上，改編輯 `support_contact_type`（None ↔ Sphere），取消選取，確認其他 auto 點的 contact sphere 顯示全程不變化
- [x] 3.3 反覆選取→編輯→取消選取多次（不同點、不同欄位），確認每一次取消選取都沒有閃現，不是偶發修好
- [x] 3.4 放大視角、降低操作速度重新測試 3.1（讓潛在的單幀閃現更容易被肉眼捕捉）

## 4. 驗證：切換選取點不引入新漏洞

- [x] 4.1 選中點 A（無 explicit geometry），不取消選取、直接改選點 B（無 explicit geometry），確認過程中其他未選取的 auto 點全程不受 A 或 B 顯示值影響
- [x] 4.2 快速連續切換選取多顆點，確認沒有任何一次切換造成其他點閃現

## 5. 驗證：不得回歸

- [x] 5.1 **live 打字語意保留**：無任何點被選取、且無借用中的 widget 時，於 Process tab 編輯欄位、尚未失焦，確認下一次重繪 auto 點的錐體直徑跟著變——與修改前行為一致
- [x] 5.2 確認 `fix-sla-support-top-params-live-read-isolation` 已驗收過的情境（選取期間其他點不受污染）依然成立
- [x] 5.3 確認 per-point Top 欄位顯示／回填（`fix-sla-support-top-config-enum-set` 驗收過的行為）不受影響——本 change 只新增一個讀取用的 getter，不改變 `begin_...`/`end_...` 本身的邏輯
- [x] 5.4 切片輸出與修改前完全相同（本 change 只影響 preview 讀值來源，不影響切片）

## 6. Follow-up（out of scope）

- 支撐點的幾何尺寸解析規則（per-point vs preset 的判定邏輯）→ `fix-sla-support-preview-geometry-source-semantics`
- `notify_process_tab_selection_changed()` 的 `CallAfter` 延遲機制本身的重新設計 → 目前判定為必要的既有設計，未列入任何 change
