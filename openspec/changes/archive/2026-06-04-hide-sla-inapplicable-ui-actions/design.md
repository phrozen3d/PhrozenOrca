## Context

OrcaSlicer 的 gizmo toolbar 與 right-click context menu 原本針對 FDM / assembly 工作流開發。PhrozenOrcaResin 加入 SLA/Resin printer profile 後，部分 UI 項目 — Brim Ears、Mesh Boolean、Assembly View、Text emboss、filament 選擇 — 在 resin 模式下變得無意義或容易誤導使用者，需要在不影響 FDM 路徑的前提下，在 SLA 模式隱藏或停用。

**相關 UI 層說明：**
- **Gizmo toolbar items** 由 `on_is_selectable()`（slot 可見性）與 `on_is_activable()`（啟用狀態）控制。`on_is_selectable()` 回傳 false 會將 slot 從 toolbar 完全移除。
- **Assembly View toolbar** 是獨立的 `GLToolbar`（`m_assemble_view_toolbar`），由 `_render_assemble_view_toolbar()` 渲染。其 item 可見性透過 `visibility_callback` 控制；因為 `m_assemble_view_toolbar.update_items_state()` 在 `on_idle()` 中被呼叫，`visibility_callback` 每幀都會被評估，能即時反應 printer profile 切換。
- **Separator toolbar**（`m_separator_toolbar`）渲染 gizmo toolbar 與 Assembly View button 之間的垂直分隔線。其 `update_items_state()` 在 `on_idle()` 中**不被**呼叫，因此 separator item 上的 `visibility_callback` 不會執行，可見性必須在 render 函式層級控制。
- **Right-click context menus** 的 Add Primitive 選項建立在 `m_default_menu` 上，該 menu 在啟動時只建立一次。若在建構時加入 ptSLA 判斷，一旦啟動時使用 SLA profile，項目就會永久缺席，之後切換至 FDM 也不會出現。使用 `wxEVT_UPDATE_UI` 動態 disable 的方式在每次 menu 開啟時評估條件，與啟動時的 profile 無關。

## Goals / Non-Goals

**Goals:**
- 在 SLA 模式下，從 gizmo toolbar 移除 Brim Ears、Mesh Boolean、Assembly View 的 slot。
- 在 SLA 模式下，完全隱藏 Assembly View toolbar group 的所有渲染結果（button、separator 線、toolbar 背景矩形）。
- 在 SLA 模式下停用 Text menu 項目，在 FDM 模式下維持可見且可用。
- 在 SLA 模式下隱藏 filament menu 項目。
- 所有 gating 均讀取 `get_edited_preset().printer_technology()`，與此 codebase 其他 gizmo 邏輯一致。

**Non-Goals:**
- 修改 backend slicing 或 print config 行為。
- 從 FDM 模式移除 Brim Ears、Mesh Boolean、Emboss 功能本體。
- 以任何方式改變 FDM 模式的 toolbar 或 menu 行為。
- 在 printer profile 切換時重新建立 `m_default_menu`，以達到 SLA 隱藏（而非 disabled）Text 的效果。
- 修改 `GLToolbar::render()` 或 `render_background()` 的通用邏輯。

## Decisions

### D1: Gizmo toolbar — 使用 `on_is_selectable()` 而非 `on_is_activable()`

**Choice**: 對 Brim Ears、Mesh Boolean、Assembly gizmo 覆寫 `on_is_selectable()`，在 SLA 時回傳 false。

**Rationale**: `on_is_selectable()` 會將 toolbar slot 完全移除，適合 SLA 模式（此工具在 resin 列印中根本不適用，而非暫時無法使用）。`on_is_activable()` 只會讓 slot 顯示為灰色，對於永遠不適用的工具而言是較差的 UX。

### D2: Assembly View toolbar 可見性 — 在 toolbar item 使用 `visibility_callback`

**Choice**: 在 `_init_assemble_view_toolbar()` 中對 Assembly View toolbar item 設定 `visibility_callback = != ptSLA`。

**Rationale**: `m_assemble_view_toolbar.update_items_state()` 在 `on_idle()` 中被呼叫，所以 `visibility_callback` 每幀都會被評估，並在 printer profile 切換時即時反應。這是此 toolbar 的正確機制。

### D3: Assembly View separator 與背景 — 在 render 函式層級 early return

**Choice**: 在 `_render_separator_toolbar_right()` 與 `_render_assemble_view_toolbar()` 兩者加入 `ptSLA` early return。

**Rationale**: `m_separator_toolbar.update_items_state()` 在 `on_idle()` 中不被呼叫，使 separator item 的 item-level `visibility_callback` 成為無效 dead code。此外，`GLToolbar::render_horizontal()` 在檢查 item 可見性前就呼叫 `render_background()`，因此即使 toolbar 內所有 item 都不可見，仍會渲染寬度為 `2 * border` 的背景矩形。在 render 函式層級 early return 是唯一可靠的抑制點，與此檔案其他 render-level guard 的模式對稱。

### D4: Text menu — 使用 `wxEVT_UPDATE_UI` 動態 disable，而非在建構時加入 guard

**Choice**: 傳入 `cb_condition = []() { return printer_technology() != ptSLA; }` 與 parent window 給 `append_menu_item()`；條件在每次 menu 開啟時透過 `wxEVT_UPDATE_UI` 評估。

**Rationale**: `m_default_menu` 在啟動時只建立一次。若以 early return 或建構時的 ptSLA 判斷，一旦啟動時使用 SLA profile，Text 項目會永久缺席，使用者之後切換至 FDM 也無法看到。`wxEVT_UPDATE_UI` 動態評估與啟動時的 profile 無關。在 printer profile 切換時重建整個 default menu 是更大範圍、風險更高的修改，不值得為此需求引入。

**Trade-off**: SLA 下 Text 保持可見但為灰色，而非隱藏。此為可接受的 UX 取捨：可告知使用者此功能在 FDM 下存在，同時不暴露無法使用的功能。

### D5: Filament menu — 在 append 時 early return

**Choice**: 在 `append_menu_item_change_filament()` 中，當 `printer_technology() == ptSLA` 時 early return。

**Rationale**: Filament 選擇在 SLA 模式下完全不適用。不同於 Text，顯示一個 disabled 項目沒有任何意義 — resin 列印中根本沒有 filament 的概念。Early return 是最簡單且正確的做法。

### D6: `printer_technology()` 讀取 `get_edited_preset()` 而非 `get_selected_preset()`

**Choice**: 將 `GUI_Factories.cpp` 中的 static `printer_technology()` helper 改為使用 `get_edited_preset()`。

**Rationale**: `GLGizmosManager` 的 gizmo 可見性邏輯使用 `get_edited_preset()`。若 menu gating 使用 `get_selected_preset()`，在 profile 已編輯但尚未儲存時，menu 與 toolbar 可能顯示不一致。`get_edited_preset()` 反映目前有效的設定。

## Risks / Trade-offs

**[Low] SLA 下 Text 為 disabled（灰色），而非 hidden**：在 `m_default_menu` 只建立一次的限制下，為可接受的 UX 取捨。

**[Low] `_render_assemble_view_toolbar()` 在 SLA 下跳過所有渲染**：SLA 模式下 `has_assmeble_view()` 的 enabling_callback 已回傳 false，因此不會損失任何 tab 切換功能；early return 只是防止空白背景矩形被繪製。

## Migration Plan

不需要 migration。所有修改均為加法式（新增 guard 或分支），未改變任何 project file 格式、config schema 或現有 API。
