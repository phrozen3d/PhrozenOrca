## Why

`fix-sla-support-top-params-live-read-isolation`（2026-08-04 archive）修好了「選中一顆點編輯 Top 參數時，其他未選取的點被連動污染」的問題：`process_top_float_live()` / `process_contact_type_is_sphere()` 在 `has_selected_support_points() == true` 時跳過讀 widget，改讀 `sla_process_config()`，避免讀到被機制二（`begin_support_point_top_field_display()`）借用來顯示選中點的 widget 文字。

驗收該 change 後，使用者發現一個殘留的時序漏洞：編輯一顆選中的 auto 點後**取消選取**（例如點掉 3D 視窗，同時觸發欄位失焦與取消選取），其他未選取的 auto 點會有一瞬間閃現成「剛剛被編輯的那顆點的外觀」，下一瞬間才跳回正確的 preset 外觀。

根因是兩個狀態沒有同步變化：`m_selection_empty = true` 是同步設定的（滑鼠一點掉選取立刻生效），但真正把 Top 欄位還原成 preset 文字的 `TabSLAPrint::end_support_point_top_field_display()`，是包在 `notify_process_tab_selection_changed()`（`GLGizmoSlaSupports.cpp:1949`）的 `wxTheApp->CallAfter(...)` 裡、延後到下一個事件迴圈才執行（註解明寫是為了避免在 gizmo 滑鼠事件處理中同步呼叫 wx `set_value()` 導致重入崩潰）。取消選取的瞬間到 `CallAfter` 真正跑完之間（可能橫跨一到多幀），`has_selected_support_points()` 已經變 false，但 widget 上其實還顯示著剛剛那顆點的借用值——`fix-sla-support-top-params-live-read-isolation` 的守門邏輯在這段空窗期會誤判「沒有點被選取」而放行讀取 widget，讀到的正是還沒被清掉的舊值。

這個問題只發生在「取消選取」方向：選取一顆點時 `has_selected_support_points()` 立刻變 true，守門邏輯立刻生效跳過 widget，不管 widget 本身多快跟上都無妨——只有「守門先關掉、widget 還沒還原」這個反向空窗才會漏。

`TabSLAPrint` 已經有一個更精確、現成的訊號可用：`m_support_point_top_field_active`（`Tab.hpp:687`），其自身註解明寫「只要 Top 欄位還顯示著選中點的值就是 true，直到 `end_support_point_top_field_display()` 真正跑完才變 false」——這正是「widget 現在是不是還借用中」，而不是「選取狀態現在是不是 true」，兩者在取消選取的瞬間會短暫不同步，前者才是本 change 該用的判斷依據。目前該旗標是私有成員，沒有 public getter。

本議題是 `fix-sla-support-top-params-live-read-isolation` 的直接延伸（同一組函式、同一個「widget 讀值隔離」問題空間），不是新的獨立軸線，也跟仍在決策中的 `fix-sla-support-preview-geometry-source-semantics`（問的是「該用哪個幾何來源」的規則本身）不同——本 change 是「現有規則的守門條件跟它要追蹤的狀態偶爾不同步」的時序 bug，即使 `fix-sla-support-preview-geometry-source-semantics` 最終採用逐欄位方案，這個時序問題大概率依然存在（逐欄位下仍需要某種「讀即時 preset 值」的 fallback 路徑，同樣會暴露在這個 widget-vs-選取旗標的空窗期）。

## What Changes

- `TabSLAPrint` 新增 public getter，暴露 `m_support_point_top_field_active`（比照現有 `is_support_point_top_field_update()` 的寫法）。
- `process_top_float_live()` 與 `process_contact_type_is_sphere()` 的守門條件改為（或疊加）檢查這個更精確的旗標，取代目前單獨依賴 `has_selected_support_points()` 的判斷。

### Non-goals

- 不改變 `has_selected_support_points()` 本身的定義或其他呼叫端的用法。
- 不改變 `notify_process_tab_selection_changed()` 的 `CallAfter` 延遲機制——那是為了避免重入崩潰的既有必要設計，本 change 不動它，而是讓讀值守門條件正確反映這個延遲的存在。
- 不改變支撐點的幾何尺寸解析規則（per-point vs preset 的判定邏輯）——那是 `fix-sla-support-preview-geometry-source-semantics` 的範圍，且是不同軸線（見上方 Why 的說明）。
- 不處理選取時的其他 UI 行為（Top 欄位顯示/回填本身），只處理讀值守門條件的精確度。

## Capabilities

### New Capabilities

<!-- 無。本 change 為既有 capability 增補 requirement。 -->

### Modified Capabilities

- `sla-support-points-preview`：修改 `fix-sla-support-top-params-live-read-isolation` 新增的 requirement「Live Top parameter reads are isolated from the per-point display borrow」，將守門條件從「是否有支撐點被選取」改為「Top 欄位是否仍在顯示某個點的借用值」，涵蓋取消選取瞬間的時序空窗。

## Impact

- **Primary**：
  - `src/slic3r/GUI/Tab.hpp` / `Tab.cpp`：`TabSLAPrint` 新增 `m_support_point_top_field_active` 的 public getter
  - `src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp`：`process_top_float_live()`、`process_contact_type_is_sphere()` 的守門條件
- **Reference（不預期修改）**：`notify_process_tab_selection_changed()`、`begin_support_point_top_field_display()` / `end_support_point_top_field_display()` 本身的邏輯與時序不變，只是被讀取的方式更精確
- 不影響切片輸出、檔案格式或 profile
- 無 public API 變更（新增的 getter 是內部 GUI 類別的存取介面，非對外 API）
