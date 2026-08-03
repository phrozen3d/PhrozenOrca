## ADDED Requirements

### Requirement: 線材通道對應區可見且可用

送印對話框（`PhrozenSelectMachineDialog`）SHALL 顯示線材通道對應區，並僅列出當前列印盤實際使用到的工具號（`get_used_extruders()`），每個通道以一個對應項目（`PhrozenMaterialItem`）呈現其材料名稱、顏色與目前對應的目標料盤。

#### Scenario: 開啟對話框顯示已使用的通道
- **GIVEN** 當前列印盤已切片且使用了 T0、T1、T2、T3 四個工具號
- **WHEN** 使用者開啟「傳送列印作業至」對話框
- **THEN** 線材通道對應區 SHALL 顯示 4 個對應項目，且每個項目預設對應其原始工具號（T0→A1、T1→A2、T2→A3、T3→A4）

#### Scenario: 僅顯示被使用的通道
- **GIVEN** 當前列印盤只使用了 T0 與 T2
- **WHEN** 使用者開啟對話框
- **THEN** 對應區 SHALL 只顯示 2 個項目（對應 T0 與 T2），不顯示未使用的通道

### Requirement: 通道目標可透過下拉選單切換

每個通道項目 SHALL 提供下拉選單，讓使用者將該通道的目標料盤切換為 A1～A4（即 T0～T3）中的任一個，且切換後 UI SHALL 即時反映新的對應目標。

#### Scenario: 將通道切換到不同料盤
- **GIVEN** 某通道目前對應 A1
- **WHEN** 使用者點擊該通道下拉選單並選擇 A3
- **THEN** 該通道項目 SHALL 顯示其目標已變為 A3，且內部映射狀態（原始工具號→目標工具號）SHALL 同步更新為 T0→T2

#### Scenario: 切回原本目標
- **GIVEN** 某通道已從 A1 改為 A3
- **WHEN** 使用者再次將其下拉選單選回 A1
- **THEN** 該通道映射 SHALL 回復為恆等（T0→T0）

### Requirement: 允許通道碰撞合併

對應映射 SHALL NOT 強制為雙射；系統 MUST 允許多個來源通道指向同一個目標料盤（代表多條線材合併使用同一捲），且不得因此阻擋使用者操作或送印。

#### Scenario: 兩個通道指向同一料盤
- **GIVEN** 通道 T0 與通道 T2 皆為使用者可調整
- **WHEN** 使用者將 T0 與 T2 都設定為目標 A4
- **THEN** 系統 SHALL 接受此設定，不顯示錯誤、不阻擋後續送印

### Requirement: 恆等對應的判定

系統 SHALL 能判定目前所有通道的對應是否整體為恆等（每個來源的目標皆等於其原始工具號），作為送印時是否需要改寫 G-code 的依據。

#### Scenario: 全部未變更
- **GIVEN** 使用者開啟對話框後未調整任何通道下拉選單
- **WHEN** 系統計算整體映射
- **THEN** 映射 SHALL 被判定為恆等

#### Scenario: 至少一個通道被變更
- **GIVEN** 使用者將其中一個通道由 A2 改為 A4
- **WHEN** 系統計算整體映射
- **THEN** 映射 SHALL 被判定為非恆等

### Requirement: 清除不相關的舊流程程式碼

本能力 SHALL 移除送印對話框中與本功能無關的 Bambu 舊流程死碼（如 `#if 0` 包住的舊送印確認、AMS／PrintJob 殘留、`assert(0)` 佔位函式），使送印按鈕僅保留進入新送印編排的單一乾淨路徑。

#### Scenario: 送印按鈕路徑單一化
- **WHEN** 使用者按下「傳送」按鈕
- **THEN** 系統 SHALL 僅執行新的送印編排流程，不進入任何已停用的 Bambu 舊確認或 AMS 對應流程
