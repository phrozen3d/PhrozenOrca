## Why

切片完成後，UI 需要立即顯示預估列印時間與樹脂用量，無需等待 PRZ 匯出。目前這兩個值只在 PRZ 匯出時於 `prz_header()` 內部計算，`SLAPrintStatistics` 完全沒有保存物理模型的列印時間。

## What Changes

- 將 `calculate_prz_print_time()` 的簽名從 `(const SLAPrint&, const DynamicPrintConfig&)` 改為 `(int total_layers, const DynamicPrintConfig&)`，並從 `static` 提升為 `PhrozenPRZ.hpp` 匯出的公開函式
- 在 `SLAPrintStatistics` 中新增 `prz_print_time_s` 欄位（int，秒）
- 在 `merge_slices_and_eval_stats` 步驟中，利用 `printer_input.size()` 呼叫上述函式，並將結果存入 `print_statistics.prz_print_time_s`
- 不包含 UI 顯示邏輯（PhrozenStatusPanel 接線留待後續 change）

## Capabilities

### New Capabilities

- `sla-print-time-in-stats`: 切片後於 `SLAPrintStatistics::prz_print_time_s` 提供完整物理模型列印時間（秒），供 UI 或其他下游使用方讀取

### Modified Capabilities

（無 spec 層級的行為變更）

## Impact

**修改的檔案：**
- `src/libslic3r/Format/PhrozenPRZ.cpp` — 移除 `static`，改參數簽名
- `src/libslic3r/Format/PhrozenPRZ.hpp` — 新增函式宣告
- `src/libslic3r/SLAPrint.hpp` — `SLAPrintStatistics` 新增欄位
- `src/libslic3r/SLAPrintSteps.cpp` — 在 merge 步驟呼叫函式並存值

**相依性：**
- `PhrozenPRZ.cpp` 需加入 `SLAPrintSteps.cpp` 的 include 鏈
- 此 change 不影響 PRZ 匯出的輸出結果（`prz_header()` 呼叫點更新為傳入 `layer_images().size()`，行為不變）
