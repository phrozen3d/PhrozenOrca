## Why

"Duplicate Current Plate" 會將一個 build plate 複製成新的 plate，屬於多 plate 工作流程的功能。PhrozenOrcaResin 在 Resin/SLA 模式下已經隱藏或停用多個 FDM / 多 plate 導向的動作（參見 `sla-ui-feature-gating`），但當初這波調整漏掉了 "Duplicate Current Plate"，導致它在 Resin/SLA 使用者的 Edit menu 中仍然顯示——而這個功能對單一 plate 的 resin 工作流程並無意義，容易造成誤導。

## What Changes

- 當目前 active printer technology 為 SLA/Resin 時，`Edit → Duplicate Current Plate` 這個 menu 項目會被隱藏（而不僅僅是 disabled）。
- 當目前 active printer technology 為 FFF/FDM 時，此項目維持現有顯示與可用行為，完全不變。
- 顯示狀態會在每次開啟 Edit menu 時重新評估，因此使用者在不重啟應用程式的情況下切換 printer technology，也能立即反映出來，與此 menu 中其他項目現有的行為一致。
- 底層的 `duplicate_plate()` 動作本身、其他呼叫路徑，以及 Edit menu 中其他項目，皆不受影響。

## Capabilities

### New Capabilities
（無）

### Modified Capabilities
- `sla-ui-feature-gating`：新增一項需求，規定 Edit menu 中的 "Duplicate Current Plate" 項目在 SLA 模式下 SHALL NOT 顯示，在 FDM 模式下不受影響。

## Impact

- `src/slic3r/GUI/MainFrame.cpp`：`MainFrame::init_menubar_as_editor()` —— 建立 "Duplicate Current Plate" `wxMenuItem` 並將其加入 `editMenu` 的位置。
- `src/slic3r/GUI/MainFrame.hpp`：新增 private member，用於追蹤該 menu item 的指標與位置，以便動態顯示/隱藏。
- 不會變更 `Plater::duplicate_plate()` 或任何其他觸發 duplicate-plate 功能的入口。
