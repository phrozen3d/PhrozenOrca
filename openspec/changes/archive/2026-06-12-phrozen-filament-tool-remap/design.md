## Context

本變更在 Phrozen Orca FDM 的「傳送列印作業至」對話框（`PhrozenSelectMachineDialog`）末端，讓使用者把任一線材通道重新對應到任一目標料盤（如 `T0 → T3`），並在送印前自動改寫 G-code 的工具號後送出。動機與七大定案見 [proposal.md](proposal.md)。

關鍵的既有事實（grill-me 階段已於程式碼驗證，作為本設計的前提）：

1. **送印路徑「不重切」**：`use_3mf=false` 時 `on_send_print` → `Plater::send_gcode_legacy` → `export_gcode(upload_job)` → `schedule_upload` → 背景流程。若 plate 已切片（`m_print->finished()==true`），背景走 `BackgroundSlicingProcess` 的「skip slicing」分支；上傳情境下 `m_upload_job` 非空，**不會呼叫 `export_gcode_from_previous_file`、不重新輸出**，`prepare_upload()` 直接複製 `PartPlate::get_tmp_gcode_path()` 上傳。
2. **改檔目標檔**＝`plate->get_tmp_gcode_path()`（等同 `m_gcode_result->filename`），切片成功後保證存在於硬碟（`is_valid_gcode_file()` 為真）。
3. **工具號的兩種來源**：裸 `T[n]` 切換行（`GCodeWriter::toolchange()`，前綴為純 `T`）與帶工具號的溫度指令 `M104/M109 ... T[n]`（`GCodeWriter::set_temperature()` 在 `tool>=0` 時附加 ` T<tool>`）。
4. **`send_gcode_legacy` 必須在 GUI 主執行緒執行**（內部 `ShowModal()` 跳 `ElegooPrintHostSendDialog`、操作 `background_process` 與場景）。
5. **目前 `on_send_print` 在結尾即 `Hide()` + `EndModal(wxID_OK)`**，本設計需延後關閉。

## Goals / Non-Goals

**Goals:**
- 末端純工具號重新對應，**不重切、不改溫度數值**。
- 全面映射：裸 `T[n]` 與 `M104/M109 ... T[n]` 預熱指令同時重寫，保證預熱的是正確料盤。
- 改檔在背景執行緒完成，**不凍結 UI、不破壞 wxWidgets 執行緒模型**。
- 對崩潰／取消具防禦性：半套檔絕不外洩，原始切片 G-code 隨時可回復。
- 重按、改選後結果永遠正確（冪等）。

**Non-Goals:**
- **不**支援「真正換材料」（不同 filament preset、不同溫度/流速）——那等同重切，明確排除。
- **不**在本功能內補做切片；未切片即阻擋。
- **不**改動 `send_gcode_legacy`／`BackgroundSlicingProcess`／`prepare_upload` 的既有行為（僅依賴其「不重切」分支）。
- **不**處理 `PLATE_ALL_IDX` 多盤一次送印的工具號重映射（目前流程以當前盤 `PLATE_CURRENT_IDX` 為主）。
- **不**強制映射為雙射——允許通道碰撞合併。

## Decisions

### D1. 攔截點：`on_send_print` 內「送印前同步改檔」，而非背景 `prepare_upload`

**選擇**：在 GUI 主執行緒的 `on_send_print` 內，於呼叫 `send_gcode_legacy` 之前完成改檔。

**理由**：背景 `prepare_upload` 執行時 Dialog 已 `EndModal` 銷毀，映射表（`m_materialList` 內的選擇）已不存在，跨執行緒取得資料困難。改在送印前處理，映射表就在手上；又因前提 1「不重切」,改好的 `get_tmp_gcode_path()` 會被原封不動上傳。

**替代方案**：
- 在 libslic3r G-code 產生管線改（寫檔前）→ 需跨執行緒 plumb 選擇、繞過快取重用，工程量與風險最大，否決。
- 包裝 print host 讀檔（記憶體中改）→ 需逐一 host 包裝，且檔案才是真相來源，否決。

### D2. 映射語意與恆等判定

- 對話框只列出 `get_used_extruders()`（已使用的工具號）。每個 `PhrozenMaterialItem` 帶「原始工具號」（預設 `extruder i → Ai`，即 `Ti`）與「使用者選擇的目標 slot」。
- 映射表 `std::map<int,int>`：`原始工具號 → 目標工具號`（`EPhrozenAmsSlot` 之 0-based 值）。
- **恆等判定**：若所有來源的目標 == 來源（無任何變更），跳過改檔，直接走原 `send_gcode_legacy`。
- **允許碰撞**：多個來源可映射到同一目標，不擋（D1 提案定案）。因此映射是「函數」而非必然雙射；查表法天然支援多對一。

### D3. G-code 重寫：單次前向掃描 + 不可變查表（避免二次覆蓋）

**演算法**（逐行、串流式）：

```
建立不可變查表 remap: 原始T -> 新T   (來自 D2)
開 ifstream(src = pristine 備份)
開 ofstream(dst = "<tmp>.partial")
while getline(src, line):
    if 每 N 行: 檢查 atomic<bool> cancel → 真則中止
    處理 line：
       - 比對「裸工具切換行」: ^\s*T(\d+)\s*(;.*)?$        → 用 remap 取代捕捉到的數字
       - 比對「帶工具號溫度行」: ^\s*M10(4|9)\b ... \bT(\d+) → 用 remap 取代 T 後數字
       - 其餘行原樣輸出
    寫入 dst
flush + close dst
```

**為何不會二次覆蓋**：讀「來源」、寫「另一個目的緩衝/檔」,每個 token 僅查一次**不可變**原始表並輸出。`T0↔T2` 互換在同一次掃描內各自獨立決定，不存在「先全改 T0→T2、再把 T2→T0」的順序污染。

**解析穩健度**：以「整行語法形態」辨識而非全域字串取代，避免誤傷座標、註解、檔名中的 `T`。需保留行尾註解與原始空白/換行風格（含 CRLF）。`M104/M109` 需容忍 `S` 與 `T` 參數順序、其他參數共存。

**替代方案**：連續多次 `replace_all`（必二次覆蓋，否決）；正則一次性全域替換（易誤傷，否決）。

### D4. 旁路檔（sidecar）保證冪等，且永不修改原檔（修正後）

實機驗證推翻了「原地覆蓋原檔」的可行性：原始 tmp gcode 在送印當下被 OrcaSlicer 的 3D 預覽 **memory-mapped**；在 Windows 上對其 rename-replace 回 `MoveFileExW → ERROR_ACCESS_DENIED(5)`,原地 `open for write` 截斷亦不可行。故架構改為：

- **永不修改原檔。** 原檔本身即 pristine 來源（只讀；映射允許 share-read）。
- 每次重映射：**讀原檔 → 寫一份全新旁路檔 `<gcode>.remapped.gcode`**（`phrozen_write_remapped_gcode`）。
- 冪等免費：每次都讀「未變動的原檔」、重生旁路檔，連續重選 / 取消後重送皆不疊加，**不再需要 `.orig` 備份與 atomic-replace**。
- 上傳由上層**重導**到旁路檔（見 D7）。

### D5. 執行緒模型：async worker + `CallAfter`，非真阻塞

```
[GUI 主執行緒]                         [背景 worker 執行緒 (純檔案 IO)]
on_send_print()
  ├ 算 remap；恆等 → 直接 send_gcode_legacy()  (原流程)
  └ 有變更:
      ├ 守門: is_slice_result_valid() && is_valid_gcode_file()?
      │         否 → MessageDialog 提示先切片, return (不開執行緒)
      ├ disable dropdown/傳送鈕；彈非阻塞 spinner「重設線材使用配置」(留「取消」)
      ├ 啟動 worker(remap, src=原檔, dst=<gcode>.remapped.gcode, &cancel) ──┐
      │   (主執行緒不 join、不卡死，event loop 持續泵動，圈圈會轉)              │ 純檔案 IO
      │                                                                      │ 讀原檔→寫旁路檔
      ◄───────────────── wxGetApp().CallAfter(完成回呼) ─────────────────────┘
      └ 完成回呼(主執行緒):
            若已取消 → 刪旁路檔、關 spinner、恢復 UI、return
            否則 → 關 spinner、send_gcode_legacy(override=旁路檔)、EndModal
```

**核心主張**：
- worker **只做純檔案 IO，絕不觸碰任何 wxWidgets 物件**。所有 UI 更新與 `send_gcode_legacy` 一律在主執行緒（透過 `CallAfter`）。
- 「阻塞等待」被改寫為**非阻塞 async 串接**：改檔與送印是主執行緒上先後兩步，永不巢狀、永不真正卡死 GUI；否則圈圈不會動、Windows 顯示「未回應」,且 `send_gcode_legacy` 內的 `ShowModal` 需要可運作的 event loop。
- **Dialog 生命週期**：worker 啟動到完成回呼之前**不得 `EndModal`**（需移除/延後現行 `on_send_print` 結尾的 `Hide()` + `EndModal`）。完成回呼捕捉 `this`,故 Dialog 必須保持存活；以「全程 disable + 攔截關閉」確保使用者無法在此期間銷毀視窗。

**worker 載體選型**：優先沿用專案既有的 `BoostThreadWorker` / `PlaterWorker`（`PhrozenSelectMachine.cpp` 已 include），以 Job 形式包裝改檔，完成於主執行緒回呼；若整合成本過高，退而使用 `std::thread` + `wxGetApp().CallAfter`。最終以「不在 worker 內碰 wx、完成一律 CallAfter」為不可違反的準則。

### D6. 取消機制：`std::atomic<bool>` + 刪旁路檔（修正後）

- 取消旗標為 `std::atomic<bool>`，spinner 的「取消」鈕只 `store(true)`,不碰 worker 內部記憶體。
- worker 每 N 行輪詢旗標；被取消即關閉並刪除半套**旁路檔**（`boost::nowide::remove`），**原檔一個 byte 都不動**,回呼判定為取消。
- 成功則旁路檔 `<gcode>.remapped.gcode` 完整就緒，交由上層重導上傳。

### D7. 上傳重導到旁路檔（取代「原子覆蓋原檔」）（修正後）

- 因原檔被預覽鎖定而不可覆蓋（D4 的 `ACCESS_DENIED`），改為**不覆蓋原檔、上傳指向旁路檔**。
- `Plater::send_gcode_legacy` 新增 `override_gcode_path` 參數；`use_3mf=false` 且非空時，將 `upload_job.upload_data.source_path` 設為旁路檔並直接 `GUI::wxGetApp().printhost_job_queue().enqueue(...)`,**跳過背景 re-export / copy**（gcode 已存在）。恆等送印 override 為空，走原流程。
- **檔案開啟全面改用 `boost::nowide`**（`ifstream`/`ofstream`/`remove`），支援 Windows 上的 UTF-8 路徑（窄字串會被 MSVC 當 ANSI）。
- 取捨：跳過 `prepare_upload` 的 `run_post_process_scripts`（Phrozen/Elegoo 通常未設定）；旁路檔保留於 `Metadata` 暫存目錄，由 print host 佇列非同步讀取後不再需要。

### D8. UI 清理範圍

- 開啟 `PhrozenSelectMachine.cpp` 線材區（目前以 `#if 1` 包住、半成品），讓 dropdown（`show_ams_selection_menu`）真正驅動映射狀態並回寫到對應 `PhrozenMaterialItem`。
- 清除不相關的 Bambu 舊流程死碼：`on_send_btn_pressed` 內 `#if 0` 大段確認流程、`on_send_print` 內 `#if 0` 的 AMS/PrintJob 殘留、`popup_filament_backup()` 等 `assert(0)` 佔位。
- 清理後 `on_send_btn_pressed` 僅保留「呼叫新的送印編排」這條乾淨路徑。

## Risks / Trade-offs

- **[改檔誤傷非工具號的 `T`/`M104`]**（座標、註解、檔名）→ 以整行語法形態辨識，不做全域字串取代；對 `M104/M109` 僅在含 `T<digit>` 參數時動，且只替換該參數數字。需以真實 Phrozen 機台 G-code 樣本回歸測試。
- **[預熱指令不帶 `T` 參數的機台變體]**（預熱寫死在 `change_filament_gcode` 模板）→ 此情況裸 `T[n]` 仍會被正確重映；模板內殘留的工具號為設計已知限制，列入 Open Questions 與測試樣本驗證。
- **[使用者在 spinner 期間操作 Dialog 導致 use-after-free / 重入]** → 全程 disable dropdown/傳送鈕、攔截 `wxEVT_CLOSE_WINDOW`,僅「取消」可按；完成回呼一律在主執行緒序列化執行。
- **[送印實際走了「重切」分支，改檔被沖掉]**（plate 未 finished、config 變髒）→ 守門條件 `is_slice_result_valid() && is_valid_gcode_file()` 阻擋；並在送印前不觸發任何使 `m_print` 失效的操作。
- **[pristine 疊加]**（重按造成二次重映）→ D4 一律從 `<tmp>.orig` 重算。
- **[Windows rename 非原子/失敗]** → D7 採 `MOVEFILE_REPLACE_EXISTING`;失敗則保留原檔、回報錯誤、不送印。
- **[碰撞合併的物理正確性]** → 依定案信任使用者（實務上為「改用同一捲線材」），不擋；風險由使用者承擔。
- **[多盤 `PLATE_ALL_IDX`]** → 本期 Non-Goal，UI 上對該情境停用重映射或退回原行為。

## Migration Plan

- 純新增功能，無資料格式/設定遷移。
- 回退策略：若映射為恆等或功能停用，完全走原 `send_gcode_legacy` 路徑，行為與現況一致。
- 上線前以真實機台 G-code 樣本（含 4 通道、含預熱指令）驗證重寫正確性與送印端到端流程。

## Open Questions

- 真實 Phrozen 機台輸出的 G-code 中，預熱是否一律以 `M104/M109 ... T[n]` 形式出現，或部分寫死在 `change_filament_gcode` 模板？需取樣確認，可能擴充重寫規則。
- 專案既有 `rename_file` 工具在 Windows 是否已設定覆蓋旗標，或需直接改用 `MoveFileExW`。
- worker 載體最終採 `BoostThreadWorker`/`PlaterWorker` 或 `std::thread + CallAfter`,待實作期評估整合成本後定案。
