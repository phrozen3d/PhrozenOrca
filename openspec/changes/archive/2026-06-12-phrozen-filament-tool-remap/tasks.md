<!--
實作原則：四個階段互相獨立、循序漸進；每個階段最後一項為「立即驗證」,
未通過驗證不得進入下一階段。嚴禁整體做完才驗證。
-->

## 1. UI 清理與 Dropdown 綁定（capability: filament-tool-remap-ui）

- [x] 1.1 清除 `PhrozenSelectMachine.cpp` 中與本功能無關的 Bambu 舊流程死碼：`on_send_btn_pressed` 內 `#if 0` 確認流程、`on_send_print` 內 `#if 0` 的 AMS/PrintJob 殘留、`popup_filament_backup()` 等 `assert(0)` 佔位
- [x] 1.2 開啟線材通道對應區（目前半成品區塊），確認 `reset_and_sync_ams_list()` 僅依 `get_used_extruders()` 建立通道項目，並預設 `extruder i → Ai`（T0→A1…）
- [x] 1.3 將 `PhrozenMaterialItem::show_ams_selection_menu` / `on_ams_selection` 的下拉選擇真正回寫到項目的「目標料盤」狀態並即時 Refresh
- [x] 1.4 在對話框新增「整體映射表」存取：能由各通道項目蒐集出 `std::map<int,int>`（原始工具號→目標工具號），並提供「是否恆等」判定
- [x] 1.5 確認映射允許碰撞（多來源→同一目標）不被任何驗證擋下
- [x] 1.6 **驗證**：局部編譯通過（MSBuild 單檔 `PhrozenSelectMachine.cpp`，RelWithDebInfo/x64，零錯誤）；UI 行為以靜態對應實作逐項確認（runtime 點擊待實機）——僅顯示已用通道、下拉可切換並即時更新、兩通道可指向同一料盤、未變更時恆等判定為真、舊 Bambu 流程不再被觸發

## 2. G-code 掃描狀態機核心算法（capability: gcode-tool-remap-rewrite）

- [x] 2.1 新增獨立的 G-code 重寫函式（純函式、無 wx 相依），輸入：來源檔路徑、目的檔路徑、不可變映射表、取消旗標參考 —— `PhrozenToolRemap.hpp`（`phrozen_remap_tool_gcode_file` / `_stream` / `_line`）
- [x] 2.2 實作裸工具切換行辨識與替換（`^\s*T(\d+)\s*(;.*)?$`），保留前置空白與行尾註解
- [x] 2.3 實作 `M104`/`M109` 的 `T(\d+)` 參數替換，容忍 `S`/`T` 參數順序與其他參數共存，且不更動 `S` 溫度數值
- [x] 2.4 確保單次前向掃描：讀來源、逐行查「不可變」表後寫目的，每個 token 僅查一次；不得用連續多次字串取代
- [x] 2.5 保留原始行尾風格（含 CRLF）與非工具號內容（座標、進給、純註解）原樣輸出
- [x] 2.6 撰寫單元測試涵蓋 spec 場景：T0↔T2 互換不二次覆蓋、`M104 S210 T0`→`M104 S210 T2`、`G1 X10 Y20`/純註解不被誤改、無 `T` 的 `M104` 不變 —— `tests/phrozen/phrozen_tool_remap_tests.cpp`
- [x] 2.7 **驗證**：局部編譯通過（cl `/utf-8 /std:c++17`）；單元測試 17/17 全綠

## 3. Pristine 備份與原子覆蓋機制（capability: gcode-tool-remap-rewrite）

- [x] 3.1 實作 pristine 備份：首次改檔前將 `plate->get_tmp_gcode_path()` 複製為 `<tmp>.orig`（已存在則沿用）；偵測重新切片時丟棄舊備份並重建 —— `phrozen_ensure_pristine_backup`（force_refresh 重建）於 `PhrozenToolRemapApply.hpp`
- [x] 3.2 改檔一律以 `<tmp>.orig` 為來源、寫入 `<tmp>.partial`，完成後再覆蓋目標檔（保證冪等）—— `phrozen_apply_tool_remap`
- [x] 3.3 實作 Windows 原子覆蓋重命名（`MoveFileExW` + `MOVEFILE_REPLACE_EXISTING`）—— `phrozen_atomic_replace`（非 Windows 退回 `std::filesystem::rename`）
- [x] 3.4 失敗/取消路徑：刪除 `.partial`，目標檔與 `.orig` 維持不變
- [x] 3.5 撰寫測試：連續兩次不同映射皆從 pristine 算起（不疊加）、取消後不留半套檔、原子覆蓋成功 —— `tests/phrozen/phrozen_tool_remap_apply_tests.cpp`
- [x] 3.6 **驗證**：局部編譯通過（cl `/utf-8 /std:c++17`）；測試 17/17 全綠（冪等不疊加、取消無殘留、Windows 原子覆蓋成功、force_refresh 重建 pristine 均驗證）

## 4. Async Worker 與 UI 轉圈圈整合編排（capability: send-with-remap-flow）

- [x] 4.1 改寫 `on_send_print`：恆等映射 → 直接走原 `do_send_to_printer()`/`send_gcode_legacy`（不備份、不改檔）
- [x] 4.2 非恆等時加入切片守門：`is_slice_result_valid() && is_valid_gcode_file()` 不成立則彈 `MessageDialog` 提示並 return，不開任何執行緒（`start_tool_remap_then_send`）
- [x] 4.3 通過守門後：`phrozen_ensure_pristine_backup` 確保備份、彈出 `PhrozenRemapProgressDialog`（自含 pulse 計時器的轉圈圈）、`lock_ui_for_remapping(true)` disable 下拉/傳送/刷新/IP、`on_cancel` 攔截 `wxEVT_CLOSE_WINDOW`、改為改檔成功後才 `EndModal`
- [x] 4.4 啟動 `std::thread` worker 執行 `phrozen_apply_tool_remap`（第 2/3 階段改檔）；worker 僅純檔案 IO，不碰任何 wx 物件、不在背景呼叫 `send_gcode_legacy`
- [x] 4.5 worker 完成以 `wxGetApp().CallAfter` 回主執行緒 `on_tool_remap_finished`：原子覆蓋已於 apply 內完成 → 關進度對話框 → 解鎖 UI → 主執行緒 `do_send_to_printer()`（含 `send_gcode_legacy` 與 `EndModal`）
- [x] 4.6 實作取消：進度對話框「取消」鈕設 `std::atomic<bool> m_remap_cancel`；worker 中止後回呼已刪 `.partial`、關對話框、恢復互動、不送印；解構子亦 join 保險
- [x] 4.7 **驗證**：單檔局部編譯通過（MSBuild，RelWithDebInfo/x64，零錯誤，`.obj` 重建確認）；端到端行為以靜態對應實作逐項確認（runtime 點擊待實機完整 build）——恆等直送、未切片被擋、轉圈圈非阻塞、改檔後送改寫版、改檔期間互動鎖定、取消後可重選重送

## 5. 端到端回歸與收尾

- [x] 5.1 以真實機台跑完整送印：實機 cube 測試全數通過——測試 1 恆等直送、測試 2 原生置灰阻擋、測試 3 旁路重寫工具號與預熱指令全數正確、測試 4 連續改選冪等性完全正確
- [x] 5.2 檢視 `openspec/changes/phrozen-filament-tool-remap` 各 spec 場景逐項勾稽，已對照三份 spec 全數符合（含修正後旁路檔/重導需求）
- [x] 5.3 **驗證**：單元測試（17/17 + 12/12）全綠；`PhrozenSelectMachine.cpp`、`Plater.cpp` 局部編譯零錯誤；實機端到端全數通過

## 6. 修正：旁路檔重導（取代原檔覆蓋）—— 實機 ACCESS_DENIED(5)

> 實機 cube（A1→A2）送印時 `MoveFileExW` 回 `ACCESS_DENIED(5)`：原始 tmp gcode 被預覽 memory-mapped，Windows 無法覆蓋。改為「永不碰原檔，讀原檔→寫旁路檔→上傳重導」。

- [x] 6.1 廢除 `.orig` 備份 + atomic_replace + `MoveFileExW`；`PhrozenToolRemapApply.hpp` 改為 `phrozen_write_remapped_gcode(src 原檔 → dst 旁路檔)`,原檔全程不變、冪等免費；取消/失敗刪旁路檔
- [x] 6.2 全面改用 `boost::nowide`（`ifstream`/`ofstream`/`remove`）支援 UTF-8 路徑（`PhrozenToolRemap.hpp` / `PhrozenToolRemapApply.hpp`）
- [x] 6.3 `Plater::send_gcode_legacy` 新增 `override_gcode_path` 參數；`use_3mf=false` 且非空時將 upload source 指向旁路檔並直接 `printhost_job_queue().enqueue`,跳過背景 re-export/copy
- [x] 6.4 `PhrozenSelectMachine`：worker 改呼叫 `phrozen_write_remapped_gcode`、新增 `m_remap_output_path`、`do_send_to_printer` 傳 override、`on_send_print` 恆等時清空 override
- [x] 6.5 同步更新 `design.md`（D4/D5/D6/D7）、`gcode-tool-remap-rewrite` 與 `send-with-remap-flow` 規格、單元測試
- [x] 6.6 **驗證**：單元測試全綠（line/stream 17/17、apply/sidecar 12/12，含「原檔位元不變」「冪等不疊加」「取消刪旁路檔」「來源不存在」）；`PhrozenSelectMachine.cpp` 與 `Plater.cpp` 單檔局部編譯零錯誤
