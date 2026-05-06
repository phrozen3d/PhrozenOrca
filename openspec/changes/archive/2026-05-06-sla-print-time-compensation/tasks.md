## 1. PrintConfig 與預設



- [x] 1.1 在 `PrintConfig.cpp`／`PrintConfig.hpp` 新增 `print_time_compensation`（bool，預設 false）與 `layer_print_time_compensation`（float，預設 0，單位秒）

- [x] 1.2 將新鍵掛入 SLA print／process 對應之 `SLAPrintObjectConfig`（或專案慣用之物件設定類別），並更新 `Preset.cpp`、必要時範例 JSON profiles（鍵位於 `PrintObjectConfig`／full config；`Preset.cpp` 已更新；`sla_print_common.json` 已含預設）



## 2. 核心時間合併邏輯



- [x] 2.1 新增共用函式（例如 `adjusted_prz_print_time_seconds`）封裝：`T_base = calculate_prz_print_time(N, cfg)`；若啟用補償則 `T_adj = T_base + c * N`，否則 `T_adj = T_base`；回傳整數秒

- [x] 2.2 於 `SLAPrintSteps::merge_slices_and_eval_stats` 使用該函式寫入 `print_statistics.prz_print_time_s`

- [x] 2.3 於 `PhrozenPRZ.cpp` 之 `prz_header`（`PrintTimes`）改用相同函式，確保與統計一致



## 3. GUI：Advanced 與彈窗



- [x] 3.1 在 `TabSLAPrint::build` 之 Advanced 區塊、`bottom_tolerance_compensation` 區塊之後 append 兩個選項列；第二列使用 `create_line_with_widget`（或專案內等價方式）加上「設定」圖示按鈕

- [x] 3.2 新增 `LayerPrintTimeCompensationDialog`（或同等命名）：預估／實際各 h、m、s；總層數；唯讀每層秒；Apply／Cancel；計算公式依 spec

- [x] 3.3 在 `ConfigManipulation`（或 Tab 之 `toggle_options`）中：主開關關閉時停用每層補償欄位與（建議）圖示



## 4. 顯示與搜尋



- [x] 4.1 稽核所有使用 `prz_print_time_s` 或重複呼叫 `calculate_prz_print_time` 顯示預估處，確保不再繞過補償後數值（顯示層讀 `prz_print_time_s`；其值已含補償；其餘路徑已改 `adjusted_prz_print_time_seconds`）

- [x] 4.2 若 `Search`／設定索引需納入新鍵，更新 `Search.cpp`／`Search.hpp`（依專案慣例）（`TabSLAPrint::init_options_list` 以 `m_config->keys()` 建表，新鍵會納入，無需改 Search）



## 5. 文案與驗證



- [x] 5.1 新增 `localization`／`_L` 字串（標題、欄位、tooltip）

- [x] 5.2 手動驗證：補償關閉時與舊版時間一致；啟用後切片統計與匯出 PRZ 檔頭 `PrintTimes` 一致；彈窗 Apply 寫入預期每層秒數（建議本機建置後實機確認）


