## Why

以 `calculate_prz_print_time` 為代表的 PRZ 物理模型預估時間，常與實際列印耗時有系統性落差；使用者需要可保存的「每層補償」設定，並在啟用時一併反映於切片後顯示的預估列印時間，無須手動心算。

## What Changes

- 在 Resin **Process → Advanced** 頁籤最下方（`Bottom Tolerance Compensation` 之後）新增：
  - **Print Time Compensation**：啟用／停用是否將補償加進預估列印時間。
  - **Layer Print Time Compensation**：每層補償秒數（單位 **s**），預設 **0**（不依設計稿其他預設）。
  - 該數值欄位右側提供圖示，開啟 **Layer Print Time Compensation Settings** 彈窗。
- 新彈窗：輸入「預估列印時間」「實際列印時間」（各為 h／m／s）、「總層數」，唯讀顯示 **每層補償時間（秒）**；公式為 `(實際 − 預估) / 總層數`（時間先換算為秒）；**Apply** 將計算結果寫入每層補償設定，**Cancel** 關閉不寫入。
- 當 **Print Time Compensation** 啟用時，對外使用的預估秒數 SHALL 為：`calculate_prz_print_time` 的結果 **加上** `每層補償秒數 × 總層數`（總層數與該函式所用層數一致）；停用時維持僅使用 `calculate_prz_print_time` 的結果。
- **BREAKING**：無；既有預設（補償關、每層 0）行為與現行一致。

## Capabilities

### New Capabilities

- `sla-print-time-compensation`：含 Process UI、彈窗互動、設定項、計算機公式與與預估時間合併之產品行為。

### Modified Capabilities

- `sla-print-time-in-stats`：`merge_slices_and_eval_stats` 寫入之 `prz_print_time_s` 與 PRZ 檔頭 `PrintTimes` 在「啟用補償」時 SHALL 使用同一套「基底 + 每層補償 × 層數」規則，並與 `openspec/specs/sla-print-time-in-stats/spec.md` 中既有敘述對齊更新。

## Impact

- **程式**：`PrintConfig`／SLA process 設定、`TabSLAPrint::build`、新 wx 對話框類別、`SLAPrintSteps::merge_slices_and_eval_stats`、`Format/PhrozenPRZ.cpp` 之 `prz_header`（`PrintTimes`）、顯示 `prz_print_time_s` 之 GUI（如 `SLASlice2DCanvas`）、`ConfigManipulation`／preset 序列化、必要時 `localization`。
- **API／格式**：PRZ 檔頭 `PrintTimes` 與切片統計一致時，行為隨補償開關與每層秒數而變（與舊檔向後相容：關閉且 0 與舊版相同）。
