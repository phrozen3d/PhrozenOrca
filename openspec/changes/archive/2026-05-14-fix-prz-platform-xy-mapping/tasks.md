## 1. 代碼修復：補上 display_width / display_height 寫回

- [x] 1.1 閱讀 `src/slic3r/GUI/SLAPrinterSettingsDialog.cpp` 的 `sync_local_to_tab()`，確認現有 `set_key_value("printable_area", ...)` 的插入位置（行號）
- [x] 1.2 在 `set_key_value("printable_area", ...)` 之後緊接補上兩行：
  ```cpp
  cfg.set_key_value("display_width",  new ConfigOptionFloat(size_x));
  cfg.set_key_value("display_height", new ConfigOptionFloat(size_y));
  ```
- [x] 1.3 確認 `sync_local_to_tab()` 的所有呼叫路徑（正常確認、Reset 按鈕）均會通過新增的寫回邏輯
- [x] 1.4 本地建置確認編譯無錯誤（`build_release_vs2022.bat slicer`）

## 2. 盤點：PRZ header 欄位清單

- [x] 2.1 閱讀 `src/libslic3r/Format/PhrozenPRZ.cpp` 的 `prz_header()` 函數，逐一列出所有寫入 PRZ binary header 的欄位：名稱、資料型別（BE int/float 幾 bytes）、讀取來源（config key 或衍生計算）
- [x] 2.2 閱讀 `prz_layer_content()` 與相關 layer header 寫入段落，補全所有 per-layer header 欄位

## 3. 盤點：SLA UI 開放的 config key 清單

- [x] 3.1 閱讀 `SLAPrinterSettingsDialog.cpp` 的 `reload_from_preset()`，列出所有從 config 讀取並顯示在 UI 的 key 名稱
- [x] 3.2 閱讀 `sync_local_to_tab()`（修復後版本），列出所有會被寫回 config 的 key 名稱
- [x] 3.3 閱讀 `src/libslic3r/PrintConfig.cpp` 中 `SLAPrinterConfig` 相關的 option 定義，確認各 key 的型別、預設值與語意

## 4. 交叉比對與分類

- [x] 4.1 對照 Task 2 與 Task 3 的清單，將所有欄位分入四類：
  - **健康對應**：UI 有且 PRZ 有，對應正確
  - **風險/斷層**：對應存在但有同步問題或語意不明確
  - **PRZ 未覆蓋**：PRZ 有但 UI 未開放設定
  - **UI 未覆蓋**：UI 有但 PRZ 無對應
- [x] 4.2 為「PRZ 未覆蓋」欄位標記擴充優先級（高/中/低），依據：使用者對列印品質的影響程度、設定複雜度、現有 firmware 支援狀況

## 5. 撰寫 .resin-devLog/PRZ_Parameter_Mapping.md

- [x] 5.1 在 `.resin-devLog/PRZ_Parameter_Mapping.md` 頂部建立 metadata 區塊，填入：審查日期、PRZ format 版本基準（V3.0）、UI 版本基準（當前 branch/commit）
- [x] 5.2 撰寫 Section 1「健康對應欄位」，以 Markdown 表格列出 UI Config Key、UI 控件位置、PRZ 欄位名稱、PRZ 資料型別、映射方式（direct / portrait-swap / derived）、健康狀態
- [x] 5.3 撰寫 Section 2「風險/斷層欄位」，以 Markdown 表格列出，含「風險說明」欄；若無發現則標記「（本次盤點無發現）」；`display_width`/`display_height` 修復前斷層須在 Section 1 對應列附上「已修復」備注
- [x] 5.4 撰寫 Section 3「PRZ 支援但 UI 未開放欄位」，表格欄位包含：PRZ 欄位名稱、資料型別、語意說明、建議 UI Config Key、擴充優先級、備注（高優先級欄位備注不得為空）
- [x] 5.5 撰寫 Section 4「UI 開放但 PRZ 無對應欄位」，以表格列出或標記「（本次盤點無發現）」，不得留空 Section
- [x] 5.6 審閱全文件，確認四個 Section 均符合 `prz-ui-parameter-mapping` spec 的格式要求（表格欄位完整、無空白 Section、metadata 完整）
