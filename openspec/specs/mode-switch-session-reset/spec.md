# Spec: Mode Switch Session Reset

## Purpose

Define the expected behavior when the user switches between Resin (SLA) and FDM print technology via the Phrozen mode-toggle button. A mode switch represents starting a completely new print session: unsaved changes must be handled, the build platform must be cleared, and the application must return to the Home page without triggering automatic slicing.

---

## Requirements

### Requirement: Mode switch shall prompt to save unsaved changes
當使用者透過 Phrozen mode-toggle 按鈕在 Resin (SLA) 與 FDM 列印技術之間切換時，若目前 session 有未保存的變更，應用程式 SHALL 在執行切換前顯示保存確認對話框。使用者選擇 Cancel SHALL 中止整個模式切換動作，選擇 Save 或 Don't Save 才繼續執行切換。

#### Scenario: User cancels save dialog — mode switch is aborted
- **WHEN** 平台上有未保存的變更（物件、preset 修改等）
- **AND** 使用者啟動 mode toggle（任意方向）
- **AND** 保存對話框顯示後使用者選擇 Cancel
- **THEN** 模式切換中止，應用程式維持在原模式
- **THEN** 平台物件與狀態不變
- **THEN** Preset 選擇不變

#### Scenario: User saves changes — mode switch proceeds
- **WHEN** 平台上有未保存的變更
- **AND** 使用者啟動 mode toggle（任意方向）
- **AND** 保存對話框顯示後使用者選擇 Save（且 save 成功完成）
- **THEN** 變更被保存
- **THEN** 模式切換繼續執行

#### Scenario: User discards changes — mode switch proceeds
- **WHEN** 平台上有未保存的變更
- **AND** 使用者啟動 mode toggle（任意方向）
- **AND** 保存對話框顯示後使用者選擇 Don't Save
- **THEN** 未保存的變更被放棄
- **THEN** 模式切換繼續執行

#### Scenario: No unsaved changes — mode switch proceeds without dialog
- **WHEN** 平台上沒有未保存的變更（已保存或平台為空）
- **AND** 使用者啟動 mode toggle（任意方向）
- **THEN** 保存確認對話框不顯示
- **THEN** 模式切換直接繼續執行

---

### Requirement: Mode switch shall clear the build platform
Resin ↔ FDM 模式切換完成後，應用程式 SHALL 清空平台上的所有物件、支撐設定與切片狀態。模式切換代表開始一個全新的列印 session，舊模式的平台內容對新模式沒有意義，不得保留。

#### Scenario: Platform is cleared after mode switch
- **WHEN** 使用者執行模式切換（無論保存對話框結果為何，只要切換未被 Cancel 中止）
- **AND** 切換前平台上有物件
- **THEN** 切換完成後平台上的所有物件被移除
- **THEN** 所有支撐設定與切片狀態被重置
- **THEN** Project dirty flags 被重置（project 視為全新未修改狀態）

---

### Requirement: Mode switch shall navigate to Home page without triggering slicing
Resin ↔ FDM 模式切換完成後，應用程式 SHALL 導航至 Home 頁面。整個切換流程（包括 preset 載入、UI 更新）SHALL NOT 自動觸發 slicing pipeline 或顯示 Preview 頁面。

#### Scenario: Mode switch navigates to Home page
- **WHEN** 使用者完成模式切換（未被 Cancel 中止）
- **THEN** 切換完成後應用程式顯示 Home 頁面
- **THEN** Preview 頁面未被自動顯示
- **THEN** 背景 slicing 流程未啟動

#### Scenario: Mode switch from Preview page navigates to Home
- **WHEN** 使用者目前在 Preview 頁面
- **AND** 使用者啟動 mode toggle（任意方向）且未選擇 Cancel
- **THEN** 切換完成後應用程式導航至 Home 頁面
- **THEN** 背景 slicing 流程未啟動

#### Scenario: Mode switch with empty build platform
- **WHEN** 平台上沒有物件
- **AND** 使用者啟動 mode toggle（任意方向）
- **THEN** 切換完成後應用程式顯示 Home 頁面
- **THEN** 背景 slicing 流程未啟動

---

### Requirement: Mode-specific state refresh remains intact after mode switch
Resin ↔ FDM mode switch SHALL 繼續正確更新所有模式特定的應用程式狀態 — 包括 printer preset、process preset、filament/resin preset、toolbar 可見性、sidebar 控制項與 UI 元素可用性 — 且新的 Home 導航與平台清空不得干擾這些更新。

#### Scenario: Printer and process preset update correctly
- **WHEN** 使用者從 FDM 切換至 Resin 模式
- **THEN** printer preset 更新為先前選擇的 Phrozen SLA 印表機
- **THEN** process preset 更新為對應的 SLA profile
- **THEN** filament/resin preset 選擇器顯示對應新技術的 preset 類型

#### Scenario: Toolbar and UI gating update correctly after switch
- **WHEN** 使用者從 Resin 切換至 FDM 模式
- **THEN** FDM-specific toolbar 按鈕（例如 Calibration）變為可見/啟用
- **THEN** SLA-specific toolbar 按鈕依現有 feature gating 隱藏或停用

#### Scenario: Sidebar control counts reflect new technology
- **WHEN** mode switch 完成
- **THEN** extruder/resin slot 數量正確更新為新技術的對應值

---

### Requirement: Manual slicing remains available after mode switch
Resin ↔ FDM mode switch 完成後，使用者 SHALL 能夠透過一般的手動切片動作發起切片與預覽，該動作 SHALL 如常導航至 Preview 並執行 slicing。

#### Scenario: User manually slices after mode switch
- **WHEN** mode switch 剛完成且應用程式在 Home 頁面
- **AND** 使用者將物件加入平台
- **AND** 使用者呼叫 Slice 動作（toolbar 按鈕或鍵盤快捷鍵 Ctrl+R）
- **THEN** slicing pipeline 正常啟動
- **THEN** 切片完成後應用程式導航至 Preview 頁面

#### Scenario: User manually opens Preview tab after mode switch
- **WHEN** mode switch 剛完成且應用程式在 Home 頁面
- **AND** 使用者將物件加入平台並手動點擊 Preview tab
- **THEN** Preview tab 變為 active
- **THEN** auto-slicing 行為遵循現有的 Preview-tab 邏輯（若有物件且條件符合則執行 reslice）
