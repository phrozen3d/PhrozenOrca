## ADDED Requirements

### Requirement: 恆等映射直接送印

按下「傳送」時，若整體映射為恆等（無任何通道變更），系統 SHALL 不進行任何 G-code 改寫，直接走既有 `send_gcode_legacy` 送印流程。

#### Scenario: 未變更通道時直送
- **GIVEN** 使用者未調整任何通道對應
- **WHEN** 使用者按下「傳送」
- **THEN** 系統 SHALL 不建立 pristine 備份、不啟動改檔，直接執行原送印流程

### Requirement: 未切片時阻擋送印

按下「傳送」且映射為非恆等時，若當前列印盤尚未切片或 G-code 已失效（`!is_slice_result_valid()` 或 `!is_valid_gcode_file()`），系統 SHALL 阻擋送印、彈出提示要求使用者先完成切片，且 MUST NOT 啟動任何背景執行緒。

#### Scenario: 尚未切片就嘗試帶變更送印
- **GIVEN** 使用者已調整通道對應，但當前列印盤尚未切片
- **WHEN** 使用者按下「傳送」
- **THEN** 系統 SHALL 顯示「請先完成切片」之提示對話框，且 SHALL NOT 啟動改檔 worker、SHALL NOT 進入送印流程

#### Scenario: G-code 檔失效
- **GIVEN** 使用者已調整通道對應，切片結果標記無效或暫存 G-code 檔不存在
- **WHEN** 使用者按下「傳送」
- **THEN** 系統 SHALL 阻擋並提示先重新切片

### Requirement: 有變更時於背景改檔並顯示進度

按下「傳送」且映射為非恆等並通過切片守門時，系統 SHALL 停留在原對話框頁面、顯示「重設線材使用配置」的進度（轉圈圈）對話框，並於背景 worker 執行緒進行 G-code 改寫。改寫期間 GUI 主執行緒 MUST NOT 被阻塞（進度動畫須持續運作）。

#### Scenario: 帶變更送印觸發改檔
- **GIVEN** 已切片、使用者已調整通道對應
- **WHEN** 使用者按下「傳送」
- **THEN** 系統 SHALL 確保 pristine 備份存在、彈出「重設線材使用配置」進度對話框、於背景啟動改檔，且主執行緒持續可回應

### Requirement: 改檔完成後安全接續送印

背景改檔完成（未取消）後，系統 SHALL 透過主執行緒回呼機制（如 `CallAfter`）回到 GUI 主執行緒，原子覆蓋目標檔、關閉進度對話框，再呼叫 `send_gcode_legacy`。`send_gcode_legacy` MUST 在 GUI 主執行緒執行，MUST NOT 由 worker 執行緒直接呼叫。

#### Scenario: 改檔成功後送出
- **GIVEN** 背景改檔已完整完成且未被取消
- **WHEN** 完成回呼於主執行緒執行
- **THEN** 系統 SHALL 原子覆蓋目標 G-code 檔、關閉進度對話框、於主執行緒呼叫 `send_gcode_legacy` 送出改寫後的檔案

#### Scenario: worker 不得碰 UI 或送印
- **WHEN** 背景 worker 執行緒進行改檔
- **THEN** worker SHALL 僅進行純檔案 IO，SHALL NOT 直接操作任何 wxWidgets 物件，SHALL NOT 直接呼叫 `send_gcode_legacy`

### Requirement: 改檔期間鎖定對話框互動並支援取消

進度對話框顯示且 worker 執行期間，系統 SHALL 停用通道下拉選單與「傳送」按鈕並攔截視窗關閉，使對話框在完成回呼前不被銷毀；同時 SHALL 提供「取消」操作。取消時系統 MUST 透過 `std::atomic<bool>` 旗標通知 worker 中止，丟棄 `.partial` 暫存檔，恢復對話框互動，且 MUST NOT 送印。

#### Scenario: 期間鎖定互動
- **GIVEN** 改檔 worker 正在執行
- **WHEN** 使用者嘗試操作下拉選單、再次按傳送、或點右上角關閉
- **THEN** 這些操作 SHALL 被停用或攔截，對話框 SHALL 維持存活直到完成回呼

#### Scenario: 使用者取消改檔
- **GIVEN** 改檔 worker 正在執行
- **WHEN** 使用者按下進度對話框上的「取消」
- **THEN** 系統 SHALL 設定取消旗標通知 worker 中止、刪除半套旁路檔、關閉進度對話框、恢復對話框互動，且 SHALL NOT 進入送印流程

### Requirement: 上傳重導至旁路檔

改檔成功後，送印流程 SHALL 將上傳來源重導至旁路檔 `<gcode>.remapped.gcode`（透過 `send_gcode_legacy` 的 `override_gcode_path` 參數），而非上傳原始切片 G-code 檔。系統 MUST NOT 覆蓋或修改被預覽鎖定的原始 G-code 檔。

#### Scenario: 上傳改寫後的旁路檔
- **GIVEN** 已切片、改檔成功並寫出旁路檔
- **WHEN** `send_gcode_legacy` 走 `use_3mf=false` 路徑且 `override_gcode_path` 非空
- **THEN** 系統 SHALL 將上傳來源設為旁路檔並直接 enqueue，SHALL NOT 上傳未重映射的原檔

#### Scenario: 恆等送印不重導
- **GIVEN** 映射為恆等（無變更），`override_gcode_path` 為空
- **WHEN** 送印
- **THEN** 系統 SHALL 走原本上傳原始 G-code 檔的流程
