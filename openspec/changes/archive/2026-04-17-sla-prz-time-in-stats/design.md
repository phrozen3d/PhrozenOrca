## Context

SLA 切片管線分兩個主要步驟：`merge_slices_and_eval_stats`（Step 8）計算體積與簡化時間，`rasterize`（Step 9）產生 bitmap 並填入 `m_layer_images`。

目前 `calculate_prz_print_time()` 定義為 `PhrozenPRZ.cpp` 內的 `static` 函式，只在 `prz_header()` 於匯出 PRZ 時被呼叫，使用 `print.layer_images().size()` 取得總層數。這讓它無法在切片階段直接使用。

`printer_input`（`m_printer_input`）在 `merge_slices_and_eval_stats` 中透過 `initialize_printer_input()` 建立，其 `.size()` 與 `layer_images().size()` 等價，代表相同的可列印層集合。

## Goals / Non-Goals

**Goals:**
- 讓 `calculate_prz_print_time` 可在切片階段呼叫（`merge_slices_and_eval_stats` 步驟內）
- 切片完成後，`SLAPrintStatistics::prz_print_time_s` 持有完整物理模型的列印時間（秒）
- 不改變 PRZ 匯出輸出（`PrintTimes` 欄位數值不變）

**Non-Goals:**
- UI 顯示邏輯（Plater → PhrozenStatusPanel 接線）
- 修改體積計算（已在 `objects_used_material` / `support_used_material` 中正確計算）
- 修改 `estimated_print_time`（簡化版時間，保留原狀）

## Decisions

### 決策 1：refactor `calculate_prz_print_time` 簽名，移除 `static`

**選擇：** 將第一個參數從 `const SLAPrint&` 改為 `int total_layers`，移除 `static`，並在 `PhrozenPRZ.hpp` 宣告。

**原因：** 函式內部對 `print` 的唯一用途是 `print.layer_images().size()`。傳入純量 `int` 可切斷對 `SLAPrint` 物件的依賴，讓函式在 `rasterize` 前即可呼叫。

**替代方案（棄用）：**
- 在 `PhrozenPRZ.hpp` 新增包裝函式：多餘，增加 API 表面積
- 在 `SLAPrintSteps.cpp` 複製時間計算邏輯：造成重複，維護成本高

### 決策 2：在 `merge_slices_and_eval_stats` 結尾呼叫，而非 `rasterize`

**選擇：** 在 `merge_slices_and_eval_stats` 的 statistics 累積迴圈結束後，`printer_input` 已確定大小，立即呼叫函式。

**原因：** 讓時間值在 rasterize 前即可讀取（例如供 UI 在等待 rasterize 期間顯示）。兩步驟取得的總層數相同，結果一致。

**替代方案（棄用）：**
- 在 `rasterize` 結尾呼叫：可行但無必要；rasterize 是耗時步驟，延後數值的可用時機

### 決策 3：呼叫時使用 `m_print->full_print_config()`

**選擇：** `SLAPrintSteps` 透過 `m_print->full_print_config()` 取得 `DynamicPrintConfig`，傳入函式。

**原因：** 這是 SLAPrintSteps 取得完整合併設定的標準方式，與 `merge_slices_and_eval_stats` 既有用法一致。

## Risks / Trade-offs

- **[風險] include 循環：** `SLAPrintSteps.cpp` 加入 `#include "Format/PhrozenPRZ.hpp"` 可能造成循環依賴。
  **緩解：** `PhrozenPRZ.hpp` 只依賴 forward declaration 和 `DynamicPrintConfig`，不 include `SLAPrint.hpp`；確認無循環後再提交。

- **[風險] `calculate_prz_print_time` 內部依賴 config key 不存在：** 若設定中缺少某個 resin config key，函式可能回傳 0 或拋出異常。
  **緩解：** 函式邏輯不變，現有 PRZ 匯出測試已覆蓋此情境；切片端使用相同 config，風險等同現狀。

- **[Trade-off] `prz_print_time_s` 為 `int`（秒）：** 與 `estimated_print_time`（double）型別不同。選擇 `int` 因為 PRZ 格式本身以整數秒儲存，保持一致性。

## Open Questions

（無）
