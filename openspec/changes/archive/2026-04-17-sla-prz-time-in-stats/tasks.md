## 1. 公開 calculate_prz_print_time（PhrozenPRZ）

- [x] 1.1 在 `src/libslic3r/Format/PhrozenPRZ.cpp` 中，移除 `calculate_prz_print_time` 的 `static` 關鍵字，並將第一個參數改為 `int total_layers`，移除函式內部 `const int total_layers = ...` 那行
- [x] 1.2 在 `src/libslic3r/Format/PhrozenPRZ.hpp` 中新增函式宣告：`int calculate_prz_print_time(int total_layers, const DynamicPrintConfig& cfg);`
- [x] 1.3 更新 `prz_header()` 內的呼叫點：將 `calculate_prz_print_time(print, cfg)` 改為 `calculate_prz_print_time(static_cast<int>(print.layer_images().size()), cfg)`

## 2. 擴充 SLAPrintStatistics

- [x] 2.1 在 `src/libslic3r/SLAPrint.hpp` 的 `SLAPrintStatistics` 結構中新增欄位：`int prz_print_time_s = 0;`
- [x] 2.2 在 `SLAPrintStatistics::clear()` 方法中新增重置：`prz_print_time_s = 0;`

## 3. 在切片步驟計算列印時間

- [x] 3.1 在 `src/libslic3r/SLAPrintSteps.cpp` 頂部加入 `#include "Format/PhrozenPRZ.hpp"`，確認無循環依賴
- [x] 3.2 在 `merge_slices_and_eval_stats` 函式的 statistics 累積迴圈結束後，新增呼叫：
  ```cpp
  print_statistics.prz_print_time_s = calculate_prz_print_time(
      static_cast<int>(printer_input.size()),
      m_print->full_print_config()
  );
  ```

## 4. 驗證

- [x] 4.1 建置確認無編譯錯誤（`build_release_vs2022.bat slicer`）
- [x] 4.2 執行 SLA 切片，確認 `print_statistics().prz_print_time_s > 0`（可透過 debugger 或臨時 log 確認）
- [x] 4.3 匯出 PRZ，以 hex editor 確認 `PrintTimes` 欄位（bytes 0–3 of header）與 `prz_print_time_s` 數值一致
