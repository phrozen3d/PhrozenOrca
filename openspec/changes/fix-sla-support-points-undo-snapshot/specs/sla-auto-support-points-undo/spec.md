# sla-auto-support-points-undo

## Overview

本 capability 規範**自動生成**之支撐點的 undo/redo 行為：快照時點、資料持久化時機，以及還原後的狀態一致性。

與相鄰 capability 的分工：`sla-supports-apply-undo-stack` 規範 Manual Apply 流程的快照必須進主 stack 且各次 Apply 可獨立還原；本 capability 補上該 capability 未涵蓋的 auto 生成情境。兩者在混合流程（先 auto 後手動）中必須同時成立。

作用中 gizmo 的 undo 路由與顯示刷新（編輯模式內按下 Ctrl+Z 的即時行為）不屬本 capability，見 `sla-supports-apply-undo-stack` 的 out-of-scope 註記與候選 change `fix-sla-supports-active-undo-routing`。

現況的結構性成因：`auto_generate()` 在點尚未產生時即拍下快照，而自動生成的點由 `get_data_from_backend()` 拉進前端快取後從不寫回 `ModelObject`（該函式註解說明寫回會中斷背景運算）。因此該快照涵蓋不到該操作的產物。

## ADDED Requirements

### Requirement: 自動生成為可還原的單一操作

按下 Auto Support 的 Apply 觸發自動生成後，該操作 SHALL 成為 undo 堆疊上的一個可還原單位。執行一次 undo SHALL 使支撐點回到該次操作**之前**的狀態——若先前已有一組支撐點，SHALL 回到那一組，而非回到「沒有任何支撐點」。

快照 SHALL 涵蓋該操作實際產生的支撐點資料。SHALL NOT 只拍下操作前的模型狀態而讓產物落在快照範圍之外。

#### Scenario: 連續兩次不同密度的自動生成

- **GIVEN** 空白狀態的物件
- **WHEN** 使用者以密度 100% 按下 Apply，待生成完成
- **AND** 改為密度 150% 再按一次 Apply，待生成完成
- **AND** 執行一次 undo
- **THEN** 支撐點回到密度 100% 那一次產生的那組點
- **AND** 該次 undo SHALL 產生可見變化，不得是無反應的空操作

#### Scenario: 再 undo 一次回到無支撐點

- **GIVEN** 承上，已執行一次 undo 且支撐點為 100% 那組
- **WHEN** 使用者再執行一次 undo
- **THEN** 支撐點回到自動生成之前的狀態（無支撐點）

#### Scenario: redo 還原自動生成的結果

- **GIVEN** 已 undo 掉一次自動生成
- **WHEN** 使用者執行 redo
- **THEN** 支撐點回到該次自動生成產生的那組點
- **AND** 應用程式不得崩潰

### Requirement: undo 不得抹除非該次操作產生的支撐點

執行 undo 時，SHALL 僅還原該次操作所改變的支撐點。先前由其他操作產生且未被該次操作移除的支撐點 SHALL 保留。

#### Scenario: 手動新增後 undo 不影響既有的自動生成點

- **GIVEN** 已自動生成 N 個支撐點
- **WHEN** 使用者切換至手動模式新增 3 個點並 Apply
- **AND** 再新增 3 個點並 Apply
- **AND** 執行一次 undo
- **THEN** 支撐點為 N + 3 個
- **WHEN** 再執行一次 undo
- **THEN** 支撐點為 N 個
- **AND** 原本自動生成的那 N 個點仍然存在

#### Scenario: 手動 commit 不改變自動生成點的可還原性

- **GIVEN** 已自動生成一組支撐點
- **WHEN** 使用者執行任意次手動編輯與 Apply，隨後 undo 回到手動編輯之前
- **THEN** 自動生成的那組點完整保留
- **AND** SHALL NOT 因手動操作被還原而一併消失

### Requirement: 還原後的支撐點狀態欄位保持一致

任何 undo 或 redo 之後，`mo->sla_points_status` SHALL 與實際還原出來的支撐點資料一致，使後續的快取重載走向正確的來源。

#### Scenario: 還原到自動生成結果

- **GIVEN** undo/redo 使支撐點回到某次自動生成的結果
- **WHEN** 後續觸發前端快取重載
- **THEN** `sla_points_status` 使重載取得該組自動生成的點
- **AND** 顯示的支撐點與資料一致

#### Scenario: 還原到手動編輯結果

- **GIVEN** undo/redo 使支撐點回到某次手動 Apply 的結果
- **WHEN** 後續觸發前端快取重載
- **THEN** `sla_points_status` 為 `UserModified`
- **AND** 重載取得 `mo->sla_support_points` 的內容，不重新執行自動生成

### Requirement: 持久化不得中斷背景運算

為使自動生成的支撐點可被 undo 涵蓋而進行的任何持久化動作，SHALL NOT 中斷正在進行的背景運算，亦 SHALL NOT 造成切片步驟反覆失效重跑。

#### Scenario: 生成完成後的持久化

- **GIVEN** 自動生成的背景運算已完成
- **WHEN** 系統將產生的支撐點納入可還原狀態
- **THEN** 已完成的切片步驟不因此被失效
- **AND** 不觸發新一輪的支撐點重新計算

#### Scenario: 生成進行中不受干擾

- **GIVEN** 自動生成的背景運算進行中
- **WHEN** 使用者未執行任何其他操作
- **THEN** 運算正常完成
- **AND** 不因持久化動作被中斷或重啟

### Requirement: 反覆 undo/redo 不得崩潰

與支撐點相關的 undo/redo 操作在任何次數與順序下 SHALL NOT 崩潰，抵達堆疊邊界時 SHALL 為安全的無操作。

#### Scenario: 連續 undo 至堆疊邊界

- **GIVEN** 已執行數次自動生成與手動編輯
- **WHEN** 使用者連續執行 undo 直到無法再還原
- **THEN** 應用程式不崩潰
- **AND** 抵達邊界後的操作為安全的無操作

#### Scenario: undo 與 redo 交錯執行

- **GIVEN** 已執行數次自動生成與手動編輯
- **WHEN** 使用者交錯執行 undo 與 redo 多次
- **THEN** 應用程式不崩潰
- **AND** 每一步的支撐點狀態與該還原點一致
