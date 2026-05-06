## Context

- 專案已在 `PhrozenPRZ.cpp` 提供 `calculate_prz_print_time(int total_layers, const DynamicPrintConfig&)`，並於 `SLAPrintSteps::merge_slices_and_eval_stats` 將結果存入 `SLAPrintStatistics::prz_print_time_s`；`PhrozenPRZ::prz_header` 另獨立呼叫同一函式填入 PRZ 檔頭 `PrintTimes`，現行規格要求兩者數值一致。
- SLA Process UI 由 `TabSLAPrint::build()` 建構；Advanced 區末尾已有 `bottom_tolerance_compensation` 相關列。

## Goals / Non-Goals

**Goals:**

- 新增兩個 process 設定（主開關 + 每層補償秒數）與計算機彈窗；啟用時將 \(c \times N\) 加在 \(T_\mathrm{base}\) 上。
- `prz_print_time_s` 與 PRZ `PrintTimes` 使用同一套合併規則，維持可追溯的一致性。
- 預設關閉補償、每層 0，與現行行為相容。

**Non-Goals:**

- 修改 `calculate_prz_print_time` 內部物理模型公式。
- 將「預估／實際」時間自動從某次列印日誌匯入（彈窗維持手動輸入）。

## Decisions

### 1. 設定鍵命名與存放位置

在 SLA **print / process** 預設對應之 config（與 `bottom_tolerance_compensation` 同層）新增例如：

- `print_time_compensation`：`coBool`，預設 `false`
- `layer_print_time_compensation`：`coFloat`，單位秒，預設 `0`

（若與既有命名慣例衝突，實作時可微調鍵名，但須同步 preset／Preset.cpp。）

### 2. 單一真相：`T_adj` 在切片步驟與 PRZ 共用

- **切片**：於 `merge_slices_and_eval_stats` 計算 `T_adj` 後寫入 `prz_print_time_s`。
- **PRZ**：於 `prz_header()` 寫入 `PrintTimes` 時，SHALL 使用與上相同的 `N`（`layer_images().size()`，應與切片時 `printer_input.size()` 一致於完成匯出時機）、同一 `cfg`，並套用相同補償邏輯。

可抽一小型共用函式（例如 `int adjusted_prz_print_time_seconds(int N, const DynamicPrintConfig&)`）置於 `PhrozenPRZ.cpp`／`.hpp` 或鄰近工具檔，避免複製貼上公式。

### 3. 整數秒

PRZ 與統計欄位為整數秒：`T_adj` 先以 double 計算再 **四捨五入** 或 **向零截斷**；設計上擇一並全專案一致（建議四捨五入至最近整數秒）。

### 4. UI 行為

- 主開關關閉時：可沿用 `ConfigManipulation` 模式將「每層補償」欄位 disable（與 `bottom_tolerance_compensation` 類比），或仍允許編輯但不影響 \(T_\mathrm{display}\)——建議 **關閉時 disable 數值與計算機**，避免混淆。
- 彈窗內 \(N \le 0\)：唯讀每層補償顯示 0，**Apply** 可禁用或寫入 0。

### 5. 其他讀取 `prz_print_time_s` 的 UI

凡顯示「Total estimation」或 PRZ 預估處（如 `SLASlice2DCanvas`），若已讀統計欄位則無需重算；若另有旁路直接呼叫 `calculate_prz_print_time`，應改為讀統一後之值或呼叫共用 `T_adj` 輔助函式。

## Risks / Trade-offs

- **[風險] PRZ 檔頭時間含「經驗補償」**：若韌體或下游工具假設 `PrintTimes` 為純物理模型，可能與實機不完全可比 → **緩解**：文件／tooltip 說明啟用補償時該欄含使用者校正；預設關閉維持舊語意。
- **[風險] 層數不一致**：若極端情況下匯出時 `layer_images().size()` 與切片時 `N` 不同 → **緩解**：以現有管線假設兩者於完成切片後一致；若未來分歧，需單點傳遞 `N`。
- **[取捨]** 每層補償可為負（實際短於預估）→ 規格允許，唯需避免 uint 溢出（使用有號型別與下限箝制）。

## Migration Plan

- 新鍵預設：關閉 + 0 → 舊專案／舊預設無需遷移。
- 若需相容舊 JSON：缺少鍵時走預設即可。

## Open Questions

- 彈窗標籤用英文（與截圖）或跟隨 i18n（`_L`）由實作階段與產品文案統一。
- 主開關預設維持 **false**；若日後需改為 true，須另開變更以免影響既有使用者預期。
