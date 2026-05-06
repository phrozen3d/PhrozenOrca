## MODIFIED Requirements

### Requirement: merge_slices_and_eval_stats 步驟計算並存入列印時間

`merge_slices_and_eval_stats` 步驟 SHALL 在 `printer_input` 建立完畢後，先令 `N = static_cast<int>(printer_input.size())`，並以 `T_base = calculate_prz_print_time(N, m_print->full_print_config())` 計算基底秒數。接著 SHALL 自 `m_print->full_print_config()` 讀取 Print Time Compensation 開關與每層補償秒數：若補償啟用，令 `T_adj = T_base + c \times N`（`c` 為每層補償秒數）；否則令 `T_adj = T_base`。最後 SHALL 將 `T_adj` 轉為整數秒（與 PRZ 檔頭使用之整數秒語意一致）並存入 `print_statistics.prz_print_time_s`。

#### Scenario: 列印時間在 rasterize 前即可讀取

- **WHEN** `merge_slices_and_eval_stats` 步驟完成、`rasterize` 步驟尚未執行
- **THEN** `sla_print->print_statistics().prz_print_time_s` 已寫入有效整數秒結果（對非空切片結果應為正值，與既有行為一致）

#### Scenario: 時間值與 PRZ 匯出一致

- **WHEN** 相同模型與設定分別執行切片完成後讀取 `prz_print_time_s`，以及匯出 PRZ 並讀取檔頭 `PrintTimes` 欄位
- **THEN** 兩者之秒數 SHALL 相同，且皆為上述 `T_adj` 之整數秒（與 `Format/PhrozenPRZ.cpp` 中寫入 `PrintTimes` 之邏輯使用相同 `N`、相同設定與相同補償規則）

#### Scenario: 補償關閉時等同僅用 calculate_prz_print_time

- **WHEN** Print Time Compensation 為停用（或每層補償為 0）
- **THEN** `prz_print_time_s` SHALL 等於 `calculate_prz_print_time(N, m_print->full_print_config())` 經整數化後之結果
