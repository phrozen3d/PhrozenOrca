## Context

`process_top_float_live()`（`GLGizmoSlaSupports.cpp:130-158`）：

```cpp
static float process_top_float_live(const char *key, float fallback)
{
    TabSLAPrint *tab = dynamic_cast<TabSLAPrint *>(wxGetApp().get_tab(Preset::TYPE_SLA_PRINT));
    if (tab) {
        Field *field = tab->get_field(key, &page);
        if (field) {
            boost::any val = field->get_value();   // ← 讀 widget 目前顯示的文字，不問是誰放的
            ...
        }
    }
    const DynamicPrintConfig &cfg = sla_process_config();   // 只有欄位不存在時才會走到這裡
    ...
}
```

`process_contact_type_is_sphere()`（`:102-126`）結構相同：先試 `tab->get_field("support_contact_type", &page)->get_value()`，欄位不存在才退回 `sla_process_config()`。

兩者都被 `read_preview_top_params_live()`（`:237-247`）呼叫，組成 `PreviewTopParams`，每幀一次，套用給所有 `use_stored_geometry == false` 的點（即全部沒有 explicit geometry 的點）。

同一組 widget 另有第二個用途：`begin_support_point_top_field_display()`（`Tab.cpp`，`fix-sla-support-top-config-enum-set` 建立）在使用者選定支撐點時，把這些 widget 的顯示文字**暫時改寫**成該點自己的參數值，供選取期間檢視／編輯。這個機制透過 `m_support_point_top_field_active` 追蹤生命週期，直到取消選取才呼叫 `end_support_point_top_field_display()` 還原。

兩個機制各自都對，問題出在 `process_top_float_live()` / `process_contact_type_is_sphere()` **無法區分**現在 widget 上的文字是「Process tab 真正的即時值」還是「借用來顯示某點的值」——它只認得到 widget 現在顯示什麼字，不認得「為什麼」。

`GLGizmoSlaSupports::has_selected_support_points()`（`GLGizmoSlaSupports.hpp:115`）已經是判斷「Top widget 現在是否被機制二借用」的既有、準確條件：

```cpp
bool has_selected_support_points() const { return m_editing_mode && !m_selection_empty; }
```

這正是 `OptionsGroup::on_change_OG()` 與 `TabSLAPrint::on_value_change()` 用來決定「該不該把編輯導向選中點」的同一個條件，也是 `begin_support_point_top_field_display()` 被觸發的前提。

## Goals / Non-Goals

**Goals:**

- 有支撐點被選取、Top widget 正在顯示該點的值時，`read_preview_top_params_live()` 套用給其他點的參數 SHALL 反映真正的 preset 值，不被選中點的顯示值污染。
- 無支撐點被選取時，行為完全不變——維持「打字到一半就即時反應」的既有語意。
- 選中點自身的 preview 不受影響（它本來就不吃 `top_params`，見下方 D2 的等價性論證）。

**Non-Goals:**

- 不改變 `begin_support_point_top_field_display()` / `end_support_point_top_field_display()` 本身。
- 不改變幾何尺寸的來源判定規則（`preview_use_stored_top()` / `has_explicit_geometry()`），那是 `fix-sla-support-preview-geometry-source-semantics` 的範圍。
- 不改變 `default_contact_sphere_radius_mm()` 以外，其餘直接呼叫 `sla_process_config()` 的既有路徑。

## Decisions

### D1. 用既有的 `has_selected_support_points()` 當守門條件，不新造判斷

實作時把守門邏輯寫成「跳過 widget 讀值區塊、直接落入函式尾端既有的 fallback」，而不是在守門分支內另外重寫一份 fallback：

```cpp
static float process_top_float_live(const char *key, float fallback)
{
    const bool widget_borrowed_by_selection = [] {
        GLGizmoSlaSupports *gizmo = GLGizmoSlaSupports::active_instance();
        return gizmo && gizmo->has_selected_support_points();
    }();

    if (!widget_borrowed_by_selection) {
        // ...原本的 widget 讀值路徑，維持不變
    }

    // 函式尾端既有的 sla_process_config() fallback，欄位不存在時本來就會走到這裡；
    // 有選取時直接跳過 widget 讀值、同樣落到這裡，不重寫第二份 fallback。
    const DynamicPrintConfig &cfg = sla_process_config();
    if (cfg.has(key))
        return float(cfg.opt_float(key));
    return fallback;
}
```

- **為何不新增一個獨立的旗標**：`has_selected_support_points()` 已經是全檔案唯一、經過驗證的「Top widget 現在是否被借用」判斷式，`OptionsGroup.cpp` 與 `Tab.cpp` 的對應邏輯都用它。用同一個條件確保「widget 何時被借用」與「widget 借用時讀值該去哪」永遠同步，不會出現兩套判斷各自維護、逐漸漂移的風險。
- **為何不直接讓 `begin_support_point_top_field_display()` 順便設一個「正在借用」旗標給讀值端查**：`has_selected_support_points()` 已經能精確推導出「借用中」這個狀態（`m_editing_mode && !m_selection_empty`，恰好就是觸發 `begin_support_point_top_field_display()` 的條件），額外造一個旗標是重複資訊，徒增兩處要同步維護的心智負擔。
- **為何不在守門分支內另外重寫 fallback（與最初的草稿不同）**：兩個函式尾端本來就各自有一份寫好、且已在「欄位不存在」情況下驗證過的 fallback 邏輯。「被選取」只是另一個該跳去 fallback 的情境，不是需要獨立處理的新邏輯分支——讓守門分支單純「不進 widget 讀值區塊」，自然落入既有 fallback，比複製一份幾乎相同的程式碼更不容易出現兩份 fallback 日後各自修改、悄悄長歪的風險。

### D2. 等價性論證：選中點自身的 preview 不受影響

`preview_sla_head_for_point()` 只有在 `use_stored_point == false` 時才會用到 `top.*`（即 `PreviewTopParams`）的值：

```cpp
const double pin_r = use_stored_point ? double(sp.head_front_radius) : live_upper_r;
```

而選中點必然 `use_stored_point == true`（`preview_use_stored_top()` 對 `point_selected == true` 直接 early return），所以選中點的 preview 從頭到尾不吃 `top_params`。本 change 只改變 `top_params` 的**來源**（widget vs `sla_process_config()`），對選中點沒有任何影響——受影響的只有其餘吃 `top_params` 的點，也就是問題描述裡「被連動」的那些 auto 點。這代表本修正不需要對選中點的 preview 做任何額外驗證，風險面純粹侷限在「其他點的 preview 是否恢復正確」。

### D3. 兩個函式都要改，不只改一個

`process_top_float_live()` 與 `process_contact_type_is_sphere()` 是兩個獨立的靜態函式，寫法相同、問題相同，`read_preview_top_params_live()` 兩者都呼叫（六個浮點欄位走前者、`support_contact_type` 走後者）。只改一個會留下另一半的污染——例如只改浮點欄位、獨漏 contact type，使用者選中一顆 Sphere 接觸類型的點時，auto 點的 contact sphere 顯示仍會被污染。

### D4. 不影響「打字到一半即時反應」的語意

守門條件只在**有點被選取**時生效。使用者在 Process tab 打字（沒有選取任何支撐點）時，`has_selected_support_points()` 為 false，讀值路徑完全走原本的 widget 讀值，`read_preview_top_params_live()` 註解裡承諾的「值仍每幀重讀，編輯即時反映在下一幀」不受任何影響——這點已由 `fix-sla-support-top-config-enum-set` 與 `fix-sla-support-preview-stored-geometry-in-auto-mode` 的驗收覆蓋過，本 change 不重新引入相關風險。

## Risks / Trade-offs

- **[守門條件本身有誤，導致誤判「未選取」]** → `has_selected_support_points()` 是全檔案既有、已被兩個獨立呼叫端使用過的成熟判斷式，非本 change 新造，風險低。

- **[遺漏其中一個函式，污染只解決一半]** → D3 已明確兩者都要改；驗收需分別測試「浮點欄位」與「contact type 下拉選單」兩種情境。

- **[`sla_process_config()` 與 widget 未失焦時的值不同步]** → 這是刻意的：一旦有點被選取，widget 已經不代表 preset 的即時值（它在顯示別的東西），此時退回 `sla_process_config()` 才是唯一正確的「真正 preset 值」來源，不存在「該用哪個」的模糊地帶。

- **[`active_instance()` 在某些呼叫時機可能回傳 nullptr]** → 兩個函式目前的寫法（`if (tab) { ... }`）已經是「拿不到就跳過、走 fallback」的防禦式寫法，新增的 `if (gizmo) { if (has_selected...) {...} }` 沿用同樣的防禦風格，`gizmo` 為 nullptr 時自然落回原本的 widget 讀值路徑，不會是新的失敗模式。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。純渲染路徑的正確性修正，可直接隨版本發布。

回退策略：兩個函式的修改各自獨立、影響範圍小，可個別 revert 恢復現況（污染重新出現，但不會有新的錯誤模式）。

## Open Questions

無。
