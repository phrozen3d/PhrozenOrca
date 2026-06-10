## Why

Resin ↔ FDM 模式切換原本會跳至 Preview 頁是有設計目的的：切換列印技術後，Prepare 頁上的設定、工具列、sidebar、preset UI 會大量變動，系統藉由離開 Prepare 讓使用者重新進入時達到刷新效果。問題在於 Preview 頁的啟動會觸發 slicing / reslice，導致使用者只是想切換列印模式，卻被帶入切片流程。

更根本的問題是：Resin 與 FDM 是兩個完全不同的列印工作流程，原平台上的模型、支撐設定、切片結果對新模式都沒有意義，應當清空。目前的行為既不詢問使用者是否保存，也不清空平台，直接切換技術後觸發 slicing，是行為不完整的模式切換。

## What Changes

- **切換前詢問保存**：若目前有未保存的變更，切換模式前先觸發「是否保存」對話框（重用現有 `Plater::close_with_confirm()`）。使用者選 Cancel 則中止切換，選 Save 或 Don't Save 才繼續。
- **切換後清空平台**：模式切換等同開始一個新工作階段，切換後清空平台上的所有物件與切片/支撐狀態（重用 `Plater::new_project(skip_confirm=true, silent=true)`）。
- **切換後導航至 Home**：不停在 Prepare，不進入 Preview，導向 Home 頁作為中介頁面，達到刷新 Prepare UI 的原設計目的。
- **不觸發 slicing / reslice**：模式切換流程中不得自動進入 Preview 或啟動切片管線。
- **保留手動切片流程**：切換後使用者手動操作 Slice / Preview 仍照既有流程運作。

## Capabilities

### New Capabilities

- `mode-switch-session-reset`：Resin ↔ FDM 模式切換時，詢問保存→清空平台→進入 Home 頁，不觸發 slicing / reslice。

### Modified Capabilities

<!-- 現有的 spec-level requirements 沒有其他更動；原 mode-switch-prepare-state capability 由本次 mode-switch-session-reset 取代。 -->

## Impact

- **src/slic3r/GUI/MainFrame.cpp** — `phrozen_apply_work_mode()` (line ~1885)：mode switch 的主要進入點，需加入 `close_with_confirm()`、`new_project(skip_confirm, silent)`、`SetSelection(tpHome)` 邏輯。
- **src/slic3r/GUI/Plater.cpp** — `close_with_confirm()` (line ~12082)：已有的公開方法，直接重用，處理 save/don't save/cancel 三種路徑。
- **src/slic3r/GUI/Plater.cpp** — `new_project()` (line ~9623)：已有的公開方法，以 `skip_confirm=true, silent=true` 重用，達成清空平台與重置 project 狀態，不觸發 tab 導航。
- **src/slic3r/GUI/Plater.cpp** — `set_current_panel()` / `do_reslice` (line ~6798)：不修改此邏輯；清空後物件為空，`do_reslice` 的 `objects.empty()` guard 天然阻止切片。Home tab 切換也不會觸發 `EVT_GLVIEWTOOLBAR_PREVIEW`。
- 無 API surface 變更；無檔案格式變更；無 profile 變更。