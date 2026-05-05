## Why

目前 Hollow 功能的操作模式為「checkbox + Preview」兩步驟：
1. 勾選 "Hollow object when slicing" checkbox，設定目前物件的 `hollowing_enable` config
2. 再按獨立的 Preview 按鈕，觸發 `reslice_until_step(slaposDrillHoles)` 產生預覽

這個流程有以下問題：
- 使用者必須執行兩個分離的動作才能看到 hollow 預覽，操作意圖不直觀
- checkbox 代表「狀態設定」，但使用者期望的是「執行動作」
- Preview 按鈕名稱不清楚何時需要按、何時不需要按
- 參數 slider 在 checkbox 未勾選時 disabled，使用者無法先調好參數再啟用 hollow

## What Changes

- Hollow checkbox 移除渲染（不顯示）；底層 `hollowing_enable` config 邏輯保留，slicing 行為不變
- 獨立 Preview 按鈕移除渲染；Preview 行為內建在新的行為按鈕中
- 新增 `Hollow` 行為按鈕：等同「勾選 Hollow + 按 Preview」一次完成
- 新增 `Remove` 行為按鈕：等同「取消勾選 Hollow + 按 Preview」一次完成
- 三個 hollow 參數（wall thickness / closing distance / quality）slider 和 input 在未 hollow 時仍可編輯

## Capabilities

### New Capabilities

- `hollow-action-buttons`: Hollow 操作透過 Hollow / Remove 行為按鈕執行，不再需要 checkbox + Preview 兩步驟

### Modified Capabilities

（無）

## Impact

| 檔案 | 修改內容 |
|------|---------|
| `src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp` | 移除 checkbox 和 Preview 按鈕的渲染；新增 Hollow / Remove 按鈕及其 handler；調整 `hollow_active` 條件讓 slider 永遠可用 |

**不修改**：`GLGizmoHollow.hpp`、`libslic3r/` 任何檔案、SLA pipeline、`hollowing_enable` config 定義與讀取邏輯