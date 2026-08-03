## Why

目前在 Phrozen Orca FDM 的「傳送列印作業至」對話框中，使用者若想更換某段列印所使用的線材通道（料盤），唯一辦法是回到 Prepare 階段調整切片參數並**重新切片**，耗時且容易出錯。實務上 4 個通道常為**同一種材料**（如圖中皆為 PLA），使用者真正想做的只是「把原本走 T0 的這段，改成從 T2 那盤出料」——這是純粹的工具號重新對應，不需要重切。本變更讓使用者能在送印前的末端直接切換通道，並自動改寫 G-code 後送出。

## What Changes

- **開啟並清理線材通道 UI**：把 `PhrozenSelectMachine.cpp` 中目前被包住、半成品的線材對應區開啟，dropdown 可將任一通道切換到任一目標（如 `T0 → T3`）。同時**清除不相關的 Bambu 舊流程與殘留程式碼**（`#if 0` 的舊送印確認、AMS 相關死碼等），確保環境乾淨。
- **新增 G-code 工具號重映射（方案 Y — 全面映射）**：採「純工具號重新對應」語意，**不修改溫度數值**；但替換範圍涵蓋裸 `T[n]` 切換行**以及** `M104/M109 ... T[n]` 等提早預熱指令，以確保預熱的是正確料盤（並為未來多噴頭預留）。演算法採**單次前向掃描 + 不可變查表**，避免 `T0→T1`、`T1→T0` 的二次覆蓋。
- **改寫送印流程為「送印前同步改檔」**：在 `on_send_print` 內，按下傳送後若映射為恆等（無變更）則直接走原 `send_gcode_legacy`；若有變更，則**停留在原頁面**，彈出轉圈圈 Dialog「重設線材使用配置」,於背景改檔，完成後才接續送印。
- **非阻塞執行緒模型（async worker + CallAfter）**：改檔在背景 worker 執行（純檔案 IO，絕不觸碰 wxWidgets 物件），完成後透過 `CallAfter` marshaling 回 GUI 主執行緒關閉 spinner 並呼叫 `send_gcode_legacy`。「阻塞等待」以非阻塞 async 串接實作，避免凍結 UI 與 `ShowModal` 死鎖。
- **未切片防禦性阻擋**：若按下傳送時 plate 尚未切片或 G-code 已失效（`!is_slice_result_valid() || !is_valid_gcode_file()`），**直接擋下並提示使用者先完成切片**，完全不開背景執行緒。
- **pristine 備份保證冪等**：第一次改檔前先把原始切片 G-code 複製一份 pristine 備份，之後**每次重映射都從 pristine 備份算起**寫到暫存檔，再以 atomic rename 覆蓋目標檔；確保使用者重按、改選後結果永遠正確。
- **可取消 + atomic rename**：改檔寫到 `.partial` 暫存檔，worker 輪詢 `std::atomic<bool>` 取消旗標；取消或失敗即刪暫存檔、原檔不動；成功才 atomic rename。
- **允許通道碰撞合併**：映射**不強制雙射**——允許多個來源通道指向同一個目標料盤（實務上確有「改用同一種線材」的情況），不擋下。

## Capabilities

### New Capabilities
- `filament-tool-remap-ui`: 送印對話框的線材通道對應 UI——開啟並清理線材區、dropdown 切換通道、映射選擇與通道碰撞（允許合併）語意、無變更時的恆等判定。
- `gcode-tool-remap-rewrite`: G-code 工具號重映射演算法——單次前向掃描查表、同時涵蓋裸 `T[n]` 與 `M104/M109 ... T[n]` 預熱指令、不修改溫度數值、pristine 備份冪等與 `.partial` + atomic rename 寫檔。
- `send-with-remap-flow`: 送印流程編排——`on_send_print` 內的恆等直送 / 有變更走改檔分支、未切片阻擋守門、轉圈圈 Dialog、async worker + `CallAfter` 非阻塞串接、可取消，以及接續既有 `send_gcode_legacy`。

### Modified Capabilities
<!-- 無既有 spec 之需求變更；本變更全部以新增 capability 形式涵蓋。 -->

## Impact

- **GUI 程式碼**：
  - [src/slic3r/GUI/PhrozenGUI/PhrozenSelectMachine.cpp](src/slic3r/GUI/PhrozenGUI/PhrozenSelectMachine.cpp)：`on_send_print()`、`on_send_btn_pressed()`、`reset_and_sync_ams_list()`、`PhrozenMaterialItem` 的 dropdown 與映射狀態、線材區 `#if 1/#if 0` 區塊清理。
  - [src/slic3r/GUI/PhrozenGUI/PhrozenSelectMachine.hpp](src/slic3r/GUI/PhrozenGUI/PhrozenSelectMachine.hpp)：新增映射表、worker / spinner 相關成員與方法宣告。
- **送印與背景流程（讀取／依賴，原則上不改其行為）**：
  - [Plater::send_gcode_legacy](src/slic3r/GUI/Plater.cpp)（`use_3mf=false` 路徑）。
  - [BackgroundSlicingProcess](src/slic3r/GUI/BackgroundSlicingProcess.cpp) 的「已切片不重切」分支（`prepare_upload` 直接上傳 `get_tmp_gcode_path()`）——本變更成立的關鍵前提。
- **改檔目標檔**：`PartPlate::get_tmp_gcode_path()`（= `m_gcode_result->filename`），參見 [PartPlate.cpp](src/slic3r/GUI/PartPlate.cpp)。
- **G-code 工具號來源語意**：`GCodeWriter::toolchange()` 與帶 `T` 參數的 `set_temperature()`，參見 [GCodeWriter.cpp](src/libslic3r/GCodeWriter.cpp)。
- **平台**：Windows atomic rename 覆蓋既有檔需使用會覆蓋的 API（如 `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING)` 或專案既有 `rename_file` 工具）。
- **相依性**：無新增第三方相依；沿用 wxWidgets（`CallAfter`）、Boost.Filesystem 與專案既有 worker 基礎設施。
