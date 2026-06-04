## Why

部分針對 FDM / assembly 工作流設計的 toolbar 按鈕與 right-click menu 項目，在使用者切換至 SLA/Resin printer profile 後仍保持可見或可點擊。這些項目在 resin 列印中不具意義或無法使用，會造成介面混亂。使用者應只看到與目前 printer technology 相符的操作選項。

## What Changes

### Gizmo toolbar
- **Brim Ears**：SLA 模式下透過 `on_is_selectable()` 回傳 false 隱藏 toolbar slot。
- **Mesh Boolean**：SLA 模式下透過 `on_is_selectable()` 回傳 false 隱藏 toolbar slot。
- **Assembly View gizmo**：SLA 模式下透過 `on_is_selectable()` 回傳 false 隱藏 toolbar slot。

### Assembly View toolbar and separator
- Assembly View toolbar button 在 SLA 模式下透過 toolbar item 的 `visibility_callback` 隱藏。
- `_render_separator_toolbar_right()` 在 SLA 模式下跳過 render，移除 gizmo toolbar 與 Assembly View 之間的 separator 線。
- `_render_assemble_view_toolbar()` 在 SLA 模式下跳過 render，防止 Assembly View 位置殘留空白的 toolbar 背景矩形。

### Right-click context menu — Text
- Add Primitive 下的 **Text** (Emboss) 項目在 SLA 模式下為 **disabled**（灰色不可點）。項目仍保持可見，讓使用者知道此功能在 FDM 模式下可用。
- disable 條件在每次 menu 開啟時動態評估，與應用程式啟動時使用的 printer profile 無關。

### Right-click context menu — Filament
- SLA 模式下，**Change Filament** 與 **Set Filament for selected items** 不會加入 menu。

所有修改均為加法式 UI gate，不影響 FDM 模式的任何行為。

## Capabilities

### New Capabilities

- `sla-ui-feature-gating`：僅適用於 FDM 或 assembly 的 UI 項目，在 active printer technology 為 SLA 時隱藏或停用。

### Modified Capabilities

（無 — 未變更任何既有 spec 層行為。）

## Impact

- **修改檔案**：
  - `src/slic3r/GUI/GLCanvas3D.cpp` — `_init_assemble_view_toolbar()`, `_render_assemble_view_toolbar()`, `_render_separator_toolbar_right()`
  - `src/slic3r/GUI/GUI_Factories.cpp` — `printer_technology()`, `append_menu_itemm_add_()`, `append_menu_item_add_text()`, `append_menu_item_change_filament()`
  - `src/slic3r/GUI/Gizmos/GLGizmoAssembly.cpp` / `.hpp` — `on_is_selectable()`
  - `src/slic3r/GUI/Gizmos/GLGizmoBrimEars.cpp` / `.hpp` — `on_is_selectable()`
  - `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.cpp` / `.hpp` — `on_is_selectable()`
- **不影響範圍**：backend slicing、FDM toolbar/menu 行為、UndoRedo、Preview/3D/Assemble tab 切換、GLToolbar 通用 render 邏輯。
