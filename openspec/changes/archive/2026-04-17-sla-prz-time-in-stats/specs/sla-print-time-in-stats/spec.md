## ADDED Requirements

### Requirement: calculate_prz_print_time 接受 total_layers 參數
`calculate_prz_print_time` 函式的第一個參數 SHALL 為 `int total_layers`（而非 `const SLAPrint&`），並 SHALL 於 `PhrozenPRZ.hpp` 公開宣告（移除 `static`）。原呼叫點 `prz_header()` SHALL 傳入 `static_cast<int>(print.layer_images().size())`，行為不變。

#### Scenario: PRZ 匯出時仍得到相同結果
- **WHEN** 匯出 PRZ 檔案時呼叫 `prz_header()`
- **THEN** 傳入 `layer_images().size()` 後計算結果與重構前一致

#### Scenario: 外部模組可直接呼叫
- **WHEN** `SLAPrintSteps.cpp` include `Format/PhrozenPRZ.hpp`
- **THEN** 可呼叫 `calculate_prz_print_time(int, const DynamicPrintConfig&)` 而不產生 linkage 錯誤

### Requirement: SLAPrintStatistics 包含 prz_print_time_s 欄位
`SLAPrintStatistics` 結構 SHALL 包含 `int prz_print_time_s` 欄位，預設值為 `0`，並在 `clear()` 方法中被重置為 `0`。

#### Scenario: 初始狀態為零
- **WHEN** 建立新的 `SLAPrintStatistics` 或呼叫 `clear()`
- **THEN** `prz_print_time_s == 0`

#### Scenario: 切片後持有有效時間
- **WHEN** SLA 切片的 `merge_slices_and_eval_stats` 步驟完成
- **THEN** `print_statistics().prz_print_time_s > 0`（對任何非空模型）

### Requirement: merge_slices_and_eval_stats 步驟計算並存入列印時間
`merge_slices_and_eval_stats` 步驟 SHALL 在 `printer_input` 建立完畢後，呼叫 `calculate_prz_print_time(static_cast<int>(printer_input.size()), m_print->full_print_config())` 並將回傳值存入 `print_statistics.prz_print_time_s`。

#### Scenario: 列印時間在 rasterize 前即可讀取
- **WHEN** `merge_slices_and_eval_stats` 步驟完成、`rasterize` 步驟尚未執行
- **THEN** `sla_print->print_statistics().prz_print_time_s` 已有正值

#### Scenario: 時間值與 PRZ 匯出一致
- **WHEN** 相同模型與設定分別執行切片與匯出 PRZ
- **THEN** `prz_print_time_s` 與 PRZ 檔頭中的 `PrintTimes` 欄位（秒）數值相同
