## ADDED Requirements

### Requirement: Resin Process Advanced 顯示列印時間補償設定

在 **SLA／Resin** 模式下，`TabSLAPrint` 的 **Process → Advanced** 頁籤中，於 **Bottom Tolerance Compensation** 區塊之後 SHALL 顯示兩項設定：

1. **Print Time Compensation**（布林）：控制是否將每層補償加總後併入預估列印時間。
2. **Layer Print Time Compensation**（浮點數，單位秒）：每層補償秒數；預設值 SHALL 為 **0**。該欄位右側 SHALL 提供可點擊之圖示，用以開啟「Layer Print Time Compensation Settings」彈窗。

#### Scenario: 預設與現行行為一致

- **WHEN** 使用者未變更新設定或預設為「補償關閉」且每層補償為 0
- **THEN** 預估列印時間之計算結果 SHALL 與僅使用 `calculate_prz_print_time` 時相同

#### Scenario: 圖示開啟彈窗

- **WHEN** 使用者點擊「Layer Print Time Compensation」列右側之設定圖示
- **THEN** 系統 SHALL 顯示標題為「Layer Print Time Compensation Settings」之強制回應視窗

### Requirement: Layer Print Time Compensation Settings 彈窗欄位與公式

彈窗 SHALL 包含：

- **Software Predicts Print Time**（或同等語意之預估列印時間）：以小時、分鐘、秒三個數值欄位輸入。
- **Actual Print Time**（實際列印時間）：同上，三欄位輸入。
- **Layer Count**（總層數）：單一整數欄位。
- **Layer Print Time Compensation**（每層補償時間）：唯讀，單位秒，顯示依公式計算之結果。

計算規則 SHALL 為：將「實際列印時間」與「預估列印時間」各自換算為秒 \(T_\mathrm{actual}\)、\(T_\mathrm{predicted}\)，令 \(N\) 為總層數；若 \(N > 0\)，每層補償秒數為 \((T_\mathrm{actual} - T_\mathrm{predicted}) / N\)；若 \(N \leq 0\)，每層補償 SHALL 顯示為 0（或明確禁止套用，見實作設計）。

#### Scenario: Apply 寫入每層補償

- **WHEN** 使用者於彈窗按下 **Apply**
- **THEN** 系統 SHALL 將目前唯讀顯示之每層補償秒數寫入 process 之「Layer Print Time Compensation」設定並關閉彈窗

#### Scenario: Cancel 不變更設定

- **WHEN** 使用者於彈窗按下 **Cancel** 或關閉視窗而不確認套用
- **THEN** process 內已保存之每層補償秒數 SHALL 不變

### Requirement: Print Time Compensation 啟用時合併預估秒數

當 **Print Time Compensation** 為啟用，且 \(N\) 為與 `calculate_prz_print_time` 所使用之總層數（與 `printer_input.size()` 一致），\(c\) 為每層補償秒數時，對外呈現與統計所使用之預估秒數 SHALL 為：

\[
T_\mathrm{display} = T_\mathrm{base} + c \times N
\]

其中 \(T_\mathrm{base} = \texttt{calculate\_prz\_print\_time}(N, \textit{cfg})\)。當補償未啟用時，\(T_\mathrm{display} = T_\mathrm{base}\)。

#### Scenario: 啟用時增加總時間

- **WHEN** Print Time Compensation 為啟用且 \(c > 0\)、\(N > 0\)
- **THEN** 顯示之預估列印時間 SHALL 大於僅使用 \(T_\mathrm{base}\) 之結果（除非 \(c = 0\)）

#### Scenario: 停用時不受每層數值影響

- **WHEN** Print Time Compensation 為停用
- **THEN** 顯示之預估列印時間 SHALL 等於 \(T_\mathrm{base}\)，與每層補償欄位暫存數值無關
