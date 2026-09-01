## 1. MainFrame member state

- [x] 1.1 在 `src/slic3r/GUI/MainFrame.hpp` 新增 private member，用來追蹤 "Duplicate Current Plate" 的 `wxMenuItem*`、它在 `editMenu` 中原始的插入位置（index），以及目前是否處於「已附加/已移除」狀態（例如 `m_duplicate_plate_menu_item`、`m_duplicate_plate_menu_item_pos`）。

## 2. 在建立 menu 時取得該項目

- [x] 2.1 在 `src/slic3r/GUI/MainFrame.cpp::init_menubar_as_editor()` 的 `#ifndef __APPLE__` 分支（約在 `MainFrame.cpp:2989`）中，將 `append_menu_item(...)` 針對 "Duplicate Current Plate" 回傳的 `wxMenuItem*` 存入新增的 member，並記錄其在 `editMenu` 中當下的 index。
- [x] 2.2 在 `#else`（`__APPLE__`）分支（約在 `MainFrame.cpp:3092`）的原生 menu bar 路徑中，套用相同的取得方式，以維持 macOS 行為一致。

## 3. 開啟 menu 時動態顯示/隱藏

- [x] 3.1 新增一個處理函式（method 或 lambda），依據目前 `wxGetApp().get_ui_printer_technology()`：當 technology 為 `ptSLA` 且項目目前仍在 menu 中時，透過 `wxMenu::Remove()` 將 "Duplicate Current Plate" 移除；當 technology 為 `ptFFF` 且項目目前已被移除時，透過 `wxMenu::Insert(pos, item)` 依記錄的 index 重新插回。並以追蹤到的「已附加/已移除」狀態，避免重複呼叫 Remove/Insert。
- [x] 3.2 在 `editMenu` 建立完成後，於 `MainFrame`（`this`）上綁定 `wxEVT_MENU_OPEN`，過濾條件為 `evt.GetMenu() == editMenu`，呼叫 3.1 的處理函式，最後呼叫 `evt.Skip()`。
- [x] 3.3 確保此處理函式與綁定只會被設定一次（不會被重複呼叫時重複綁定），並且不會干擾 Edit menu 其他項目既有的 `cb_condition` / UPDATE_UI 綁定機制。

## 4. 驗證

- [x] 4.1 建置應用程式（`build_release_vs2022.bat slicer` 或等效的增量建置），並手動驗證：在啟用 FFF/FDM printer profile 的情況下，Edit → "Duplicate Current Plate" 顯示正常，且點擊後能正常複製目前的 plate，與現況一致。已對既有 `build-dbginfo`（RelWithDebInfo）tree 做增量編譯（目標 `PhrozenOrca_app_gui`），成功產出 `phrozen-orca.exe`；實際啟動程式，透過內建的 "Phrozen Orca Resin/FDM" 模式切換鈕切到 FDM 模式後，開啟 Edit menu 確認 "Duplicate Current Plate" 正常顯示（非灰階）並可點擊，點擊後程式無錯誤、無當機。
- [x] 4.2 手動驗證：在啟用 Resin/SLA printer profile 的情況下，Edit menu 中不會出現 "Duplicate Current Plate"，且 Edit menu 中其他項目的顯示/啟用狀態皆未受影響。已實測：預設以 Phrozen Sonic Mighty Revo 16K（Resin/SLA）啟動，開啟 Edit menu 確認 "Duplicate Current Plate" 未出現，其餘項目（Undo、Redo、Cut、Copy、Paste、Delete Selected、Delete All、Clone Selected、Select All、Deselect All）皆正常顯示，enabled/disabled 狀態與修改前一致。（更正：此處原記錄「兩個 separator 合併為一條」，經使用者提供實際截圖比對後確認判斷錯誤，實際上留下了兩條相鄰分隔線；已在第 5 節修正並重新驗證。）
- [x] 4.3 手動驗證：在不重啟應用程式的情況下，於執行期切換 active printer profile（FDM ↔ Resin/SLA），下次開啟 Edit menu 時，該項目的顯示狀態會正確更新。已透過應用程式內建的 "Phrozen Orca Resin/FDM" 模式切換鈕（`MainFrame::phrozen_apply_work_mode()`）在同一個執行中的程式實例裡從 Resin 切到 FDM，未重啟應用程式，下次開啟 Edit menu 時 "Duplicate Current Plate" 立即正確顯示；驗證過程中也發現最初以 `evt.GetMenu() == editMenu` 過濾 `wxEVT_MENU_OPEN` 的寫法在此 topbar 下拉選單架構下不會觸發（見 design.md D1 更新），已修正為不過濾、每次都重新評估。
- [x] 4.4 確認沒有動到其他呼叫 `Plater::duplicate_plate()` 的入口（可 grep `duplicate_plate(` 的所有呼叫點，並與此次變更內容比對確認）。已 grep 全 repo，`m_plater->duplicate_plate()` 僅存在於 `MainFrame.cpp:2992` 與 `MainFrame.cpp:3097` 這兩個 Edit menu 的既有 handler 中（本次未修改其邏輯，僅調整外層項目的顯示/隱藏），沒有其他呼叫點。

## 5. 修正：隱藏項目時一併隱藏多餘的 separator

- [x] 5.1 在 `src/slic3r/GUI/MainFrame.hpp` 新增 `wxMenuItem* m_duplicate_plate_menu_item_separator` member，追蹤緊接在 "Duplicate Current Plate" 之後的那個 separator。
- [x] 5.2 在 `MainFrame.cpp` 的 Windows/Linux 與 macOS 兩個分支中，把該項目之後的 `editMenu->AppendSeparator()` 呼叫改為把回傳值存入 `m_duplicate_plate_menu_item_separator`。
- [x] 5.3 修改 `update_duplicate_plate_menu_item_visibility()`：隱藏時一併 `Remove()` 該 separator；顯示時先 `Insert()` 項目回原始 index，再於 index+1 處 `Insert()` 該 separator，確保 FDM 模式下的排列（separator、項目、separator、Select All）與修改前完全一致。
- [x] 5.4 重新增量建置並手動截圖驗證：SLA 模式下該位置只剩一條 separator（不再是兩條相鄰的線）；FDM 模式下 "Duplicate Current Plate" 與其前後 separator 排列與原本一致。
