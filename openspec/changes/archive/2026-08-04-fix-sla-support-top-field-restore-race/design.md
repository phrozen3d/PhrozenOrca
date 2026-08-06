## Context

`process_top_float_live()`（`GLGizmoSlaSupports.cpp:142`）與 `process_contact_type_is_sphere()`（`:102`）已於 `fix-sla-support-top-params-live-read-isolation` 加上守門邏輯：

```cpp
const bool widget_borrowed_by_selection = [] {
    GLGizmoSlaSupports *gizmo = GLGizmoSlaSupports::active_instance();
    return gizmo && gizmo->has_selected_support_points();
}();

if (!widget_borrowed_by_selection) {
    // ...讀 widget
}
// ...否則落到 sla_process_config() fallback
```

`has_selected_support_points()`（`GLGizmoSlaSupports.hpp:115`）定義為 `m_editing_mode && !m_selection_empty`——純粹反映**選取狀態**，在支撐點被選取/取消選取的當下**同步**變化。

但 widget 實際顯示什麼文字，是由 `TabSLAPrint::begin_support_point_top_field_display()` / `end_support_point_top_field_display()`（`Tab.cpp:7293-7323`）控制，而觸發它們的 `notify_process_tab_selection_changed()`（`GLGizmoSlaSupports.cpp:1949`）把實際呼叫包在 `wxTheApp->CallAfter(...)` 裡：

```cpp
void GLGizmoSlaSupports::notify_process_tab_selection_changed()
{
    // Defer sidebar updates: calling Tab field set_value synchronously from gizmo mouse
    // handlers can re-enter wx event processing and crash.
    const bool editing       = m_editing_mode;
    const bool has_selection = !m_selection_empty;
    wxTheApp->CallAfter([this, editing, has_selection]() {
        ...
        if (!editing || !has_selection) {
            sla_tab->end_support_point_top_field_display();
            return;
        }
        ...
        sla_tab->begin_support_point_top_field_display(cfg);
    });
}
```

`m_selection_empty` 的變更是同步的（呼叫端如 `select_point()` 立刻設定），但 widget 的實際還原/借用要等到這個 `CallAfter` 排程的 lambda 真正執行——可能是下一個 wx 事件迴圈 idle tick，橫跨一到多個渲染幀。**選取旗標與 widget 內容因此可能短暫不同步**，而目前的守門條件只看前者。

`TabSLAPrint` 另有一個更精確的既有旗標，目前沒被讀值端使用：

```cpp
// Tab.hpp:682-687
// Persists for as long as the Top fields are showing a selected point's per-point values
// (from begin_support_point_top_field_display() until end_support_point_top_field_display()
// runs). Unlike m_support_point_top_field_update, this does NOT get reset by CallAfter —
// it must still be true whenever the user later deselects, or end_support_point_top_field_display()
// has nothing to tell it that a restore-to-preset is due.
bool m_support_point_top_field_active = false;
```

這個旗標由 `begin_support_point_top_field_display()` 設為 true，直到 `end_support_point_top_field_display()`**真正執行**才變回 false——精確對應「widget 現在是不是還顯示著借用值」，不受 `CallAfter` 延遲影響（本身就是為了跨越這個延遲而存在的旗標）。目前是私有成員，沒有 public getter。

## Goals / Non-Goals

**Goals:**

- 取消選取的瞬間到 `end_support_point_top_field_display()` 真正執行之間的空窗期，`process_top_float_live()` / `process_contact_type_is_sphere()` 不得讀到 widget 上尚未被還原的借用值。
- 選取的瞬間到 `begin_support_point_top_field_display()` 真正執行之間的對稱空窗期（見 D2），同樣不得讀到 widget 上尚未更新的舊值。
- 沒有任何選取／借用狀態時，行為完全不變——維持既有的「打字到一半即時反應」語意。

**Non-Goals:**

- 不改變 `notify_process_tab_selection_changed()` 的 `CallAfter` 延遲機制本身——那是為了避免 gizmo 滑鼠事件處理中重入 wx 導致崩潰的必要設計，不在本 change 範圍內重新設計。
- 不改變 `has_selected_support_points()` 的定義或其他呼叫端（`OptionsGroup.cpp`、`Tab.cpp` 的寫入路徑）的用法——那些路徑本身沒有這個時序問題（見 D2）。
- 不改變支撐點的幾何尺寸解析規則——`fix-sla-support-preview-geometry-source-semantics` 的範圍。

## Decisions

### D1. 新增 `m_support_point_top_field_active` 的 public getter，比照既有寫法

```cpp
// Tab.hpp，緊鄰現有的 is_support_point_top_field_update()
bool is_support_point_top_field_active() const { return m_support_point_top_field_active; }
```

沿用該類別既有的 getter 命名與寫法慣例（`is_support_point_top_field_update()` 同一行內聯定義），不新增額外的抽象層。

### D2. 守門條件改為兩個旗標的**聯集**（OR），不是單純替換

直覺的做法是把守門條件從 `has_selected_support_points()` **換成** `tab->is_support_point_top_field_active()`。但這樣會在**選取方向**引入一個對稱的新漏洞：

- 選取一顆點：`m_selection_empty` 同步變 false（`has_selected_support_points()` 立刻變 true），但 `begin_support_point_top_field_display()`（把 `m_support_point_top_field_active` 設為 true、同時把 widget 填入該點的值）一樣是透過 `notify_process_tab_selection_changed()` 的 `CallAfter` 延後執行。
- 若守門條件只看 `is_support_point_top_field_active()`，這段延遲窗口內它還是 false（因為 `begin_...` 還沒跑），守門邏輯會誤判「沒有借用」而放行讀 widget——這時 widget 可能還顯示著**前一個狀態**的文字（例如剛從選中點 A 切到點 B，widget 暫時還沒更新成 B 的值），一樣會讀到不該讀的東西，只是換成另一個方向的錯誤。

**決定**：兩個旗標都保留，守門條件改為聯集：

```cpp
const bool widget_borrowed_by_selection = [] {
    GLGizmoSlaSupports *gizmo = GLGizmoSlaSupports::active_instance();
    if (gizmo && gizmo->has_selected_support_points())
        return true;
    TabSLAPrint *tab = dynamic_cast<TabSLAPrint *>(wxGetApp().get_tab(Preset::TYPE_SLA_PRINT));
    return tab && tab->is_support_point_top_field_active();
}();
```

- `has_selected_support_points() == true` 這段：保護「剛選取，widget 還沒被 `begin_...` 更新」的窗口——選取當下就守住，不管 widget 現在顯示什麼。
- `is_support_point_top_field_active() == true` 這段：保護「剛取消選取，widget 還沒被 `end_...` 還原」的窗口——這是本 change 要補的那一半。
- 兩者聯集後，只有「真正沒有選取、且 widget 也確實不再借用（`end_...` 已經跑完）」才會放行讀 widget，等同兩個旗標各自守住一個方向的空窗，互不依賴誰先誰後。

### D3. 為何不索性移除 `has_selected_support_points()` 這段、只留 `is_support_point_top_field_active()`

考慮過的另一個方案：既然 `is_support_point_top_field_active()` 更精確，是否乾脆只用它，等 `begin_support_point_top_field_display()` 真正執行後守門才生效？

**不採用**，理由見 D2 的分析——這會在選取方向重新引入一個對稱的漏洞（切換選取點時短暫讀到舊 widget 內容）。`has_selected_support_points()` 本身沒有錯，它在「選取」方向是充分且必要的守門條件；問題只出在「取消選取」方向少了對應的保護。聯集兩個條件，各自守住自己擅長的方向，比丟掉其中一個再引入新問題更穩妥。

## Risks / Trade-offs

- **[聯集條件是否會讓守門「關太久」，影響 live 打字語意]** → 兩個旗標各自只在其負責的窄窗口內為 true，正常情況下（沒有選取、沒有借用）兩者皆為 false，守門不生效，行為與修改前相同。只有「剛選取」或「剛取消選取」這兩個短暫窗口內才會多守一下，不影響其餘時間的即時語意。

- **[`TabSLAPrint::active_instance()` 或 `wxGetApp().get_tab()` 在某些呼叫時機可能回傳 nullptr]** → 沿用既有的防禦式寫法（`tab &&`），nullptr 時該條件視為 false，不影響另一個條件獨立生效。

- **[新增 public getter 是否擴大了 `TabSLAPrint` 的公開介面]** → 影響面極小，getter 是唯讀存取單一 bool，與既有的 `is_support_point_top_field_update()` 同性質，不引入新的耦合面。

## Migration Plan

無資料遷移、無檔案格式或 profile 變更。純讀值守門條件精進，可直接隨版本發布。

回退策略：`Tab.hpp` 的 getter 與 `GLGizmoSlaSupports.cpp` 的守門條件改動皆可獨立 revert，回到只用 `has_selected_support_points()` 的現況（取消選取瞬間的閃現問題會重新出現，但不會有新的錯誤模式）。

## Open Questions

無。
