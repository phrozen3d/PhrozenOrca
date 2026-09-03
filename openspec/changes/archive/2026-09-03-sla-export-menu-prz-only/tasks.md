## 1. 調整右上角匯出下拉選單（SLA 分支）

- [x] 1.1 在 [src/slic3r/GUI/MainFrame.cpp](src/slic3r/GUI/MainFrame.cpp) `MainFrame::create_side_tools()` 的 `m_print_option_btn` click handler 中（約 2114-2330 行），把「Print」（`send_gcode_btn`，2159-2174 行、`IsPhrozenDeveloperMode()` 分支）的建立與 `p->append_button(send_gcode_btn)` 呼叫收斂為只在 `bIsFDMMode == true` 時執行。
- [x] 1.2 把 `export_sliced_file_btn`（2186-2187, 2234-2241 行）與 `export_all_sliced_file_btn`（2189-2190, 2243-2250 行）的 `p->append_button(...)` 呼叫（原 2297-2298 行）收斂為只在 `bIsFDMMode == true` 時執行；SLA 分支不再 append 這兩個按鈕。
- [x] 1.3 確認既有的 `export_prz_btn`（2312-2324 行，`else`/SLA 分支）與 `export_gcode_btn`（2300-2311 行，`if (bIsFDMMode)` 分支）邏輯維持不變，SLA 分支最終只 append `export_prz_btn` 一個按鈕。
- [x] 1.4 確認 `print_plate_btn`（2178-2181, 2192-2201, 2275-2276 行）、`support_send`/`support_print_all` 相關的 `send_to_printer_btn`、`send_to_printer_all_btn`、`print_all_btn`（2252-2283 行）、`enable_multi_machine` 的 `print_multi_machine_btn`（2284-2296 行）在 SLA 模式下本來就不會被 append（因 `bIsPhrozenVender` 恆為 `true` 導致 `support_send`/`support_print_all` 為 `false`，且 `print_plate_btn` 只在 `bIsFDMMode` 建立），不需額外修改；若發現例外情況需一併收斂到 `bIsFDMMode` 判斷內。
- [x] 1.5 確認未修改 `m_print_option_btn`（展開箭頭，1986, 1996-1997 行）、`SidePopup` 建立/顯示邏輯（2116-2121, 2328 行）、FDM 分支（`if (bIsFDMMode) {...}`，2300-2311 行）程式碼本身。

## 2. 驗證（右上角下拉選單）

- [x] 2.1 建置專案，切換到一個 Resin/SLA 印表機 profile，載入模型並完成切片，點擊右上角匯出按鈕旁的展開箭頭，確認下拉選單中只顯示「Export PRZ file」，箭頭本身維持顯示且可點擊。
- [x] 2.2 在 SLA 模式下點擊「Export PRZ file」，確認主匯出按鈕 label 變為「Export PRZ file」，點擊主按鈕能正常觸發 PRZ 匯出（`EVT_GLTOOLBAR_EXPORT_PRZ` → `ExportPRZJob`）。
- [x] 2.3 切換到一個 FDM 印表機 profile，完成切片，展開同一個下拉選單，確認選單內容與修改前一致（Export G-code file 及其他既有條件式項目皆正常顯示）。
- [x] 2.4 在應用程式執行期間於 FDM 與 SLA profile 間切換，重新展開下拉選單，確認選單內容即時反映目前的 printer technology，而非固定為啟動時的狀態。
- [x] 2.5 確認 File → Export 子選單以外的其他匯出入口（右上角下拉選單本身、`export_prz`/`export_gcode`/`export_sliced_file` 等功能本體）在 SLA 與 FDM 模式下皆未受本次修改影響。

## 3. 隱藏 File → Export 子選單中的切層格式項目（SLA 模式）

- [x] 3.1 在 [src/slic3r/GUI/MainFrame.hpp](src/slic3r/GUI/MainFrame.hpp) 新增 private members：`wxMenu* m_export_menu`、`wxMenuItem* m_export_sliced_file_menu_item`、`wxMenuItem* m_export_all_sliced_file_menu_item`、`wxMenuItem* m_export_gcode_menu_item`、`int m_export_sla_hidden_items_pos`、`bool m_export_sla_hidden_items_attached`，命名與型別比照既有的 `m_duplicate_plate_menu_item` 系列成員；並宣告 `void update_export_menu_sla_gated_items_visibility();`。
- [x] 3.2 在 [src/slic3r/GUI/MainFrame.cpp](src/slic3r/GUI/MainFrame.cpp) `init_menubar_as_editor()` 建立 `export_menu` 處（約 2906-2929 行），於 append「Export plate sliced file」之前記錄 `m_export_sla_hidden_items_pos = export_menu->GetMenuItemCount();`，並把「Export plate sliced file」（2918 行）、「Export all plate sliced file」（2922 行）、「Export G-code」（2926 行）三個 `append_menu_item(...)` 呼叫的回傳值分別存入 `m_export_sliced_file_menu_item`、`m_export_all_sliced_file_menu_item`、`m_export_gcode_menu_item`；將 `export_menu` 指標存入 `m_export_menu`，並把 `m_export_sla_hidden_items_attached` 初始化為 `true`。
- [x] 3.3 新增 `MainFrame::update_export_menu_sla_gated_items_visibility()`：依 `wxGetApp().get_ui_printer_technology() == ptFFF` 判斷 `should_show`；由 FDM 變 SLA 時依序 `Remove(m_export_gcode_menu_item)` → `Remove(m_export_all_sliced_file_menu_item)` → `Remove(m_export_sliced_file_menu_item)`；由 SLA 變 FDM 時依序 `Insert(pos, sliced_file)` → `Insert(pos+1, all_sliced_file)` → `Insert(pos+2, gcode)`；更新 `m_export_sla_hidden_items_attached`。實作邏輯與既有的 `update_duplicate_plate_menu_item_visibility()`（[MainFrame.cpp:3830](src/slic3r/GUI/MainFrame.cpp)）保持一致風格。
- [x] 3.4 在既有的 `this->Bind(wxEVT_MENU_OPEN, ...)` handler（約 3159 行，目前呼叫 `update_duplicate_plate_menu_item_visibility()`）內追加一行呼叫 `update_export_menu_sla_gated_items_visibility()`，不新增額外的事件綁定。
- [x] 3.5 確認未變動 `export_menu` 中其餘四個項目（「Export all objects as one STL」「Export all objects as STLs」「Export Generic 3MF」「Export Preset Bundle」）的建立、綁定與 enable 條件。
- [x] 3.6（實測發現後追加）在 [src/slic3r/GUI/MainFrame.cpp:858](src/slic3r/GUI/MainFrame.cpp) 的全域鍵盤事件 handler 中，把 `else if (evt.CmdDown() && evt.GetKeyCode() == 'G') { if (can_export_gcode()) {...} }` 的判斷式加上 `wxGetApp().get_ui_printer_technology() == ptFFF &&`，讓 `Ctrl+G` 快捷鍵在 SLA 模式下不再繞過已隱藏的選單觸發 `EVT_GLTOOLBAR_EXPORT_SLICED_FILE`。確認相鄰的 `Ctrl+Shift+G`（Print/Send Gcode，845-857 行）與其他按鍵判斷不受影響。

## 4. 驗證（File → Export 子選單）

- [x] 4.1 建置專案，切換到 Resin/SLA profile，開啟 File 選單並展開 Export 子選單，確認「Export plate sliced file」「Export all plate sliced file」「Export G-code」三項不出現，其餘四項（STL x2、Generic 3MF、Preset Bundle）正常顯示。
- [x] 4.2 切換到 FDM profile，展開同一個 Export 子選單，確認全部項目皆正常顯示，且點擊「Export plate sliced file」「Export all plate sliced file」「Export G-code」行為與修改前一致。
- [x] 4.3 應用程式執行期間於 FDM 與 SLA profile 間切換，重新展開 Export 子選單，確認三個項目的顯示/隱藏狀態即時反映目前的 printer technology。
- [x] 4.4 在 SLA 模式下按下 `Ctrl+G`（「Export plate sliced file」原本的 accelerator）。**第一次實測發現會觸發「另存 .3mf」（bug，根因為 MainFrame.cpp:858 的全域鍵盤 handler，已於 task 3.6 修補）**；修補後重新建置、重新驗證：FDM 模式下 `Ctrl+G` 正常觸發「Export plate sliced file」，SLA 模式下不再觸發另存 .3mf，兩者皆確認正常。SLA 模式下 `Ctrl+G` 改為觸發 Preview 頁籤既有的「跳轉到指定層數」（`GLCanvas3D.cpp:3711`，`IMSlider::show_go_to_layer()`）並可正確運作——經排查（`git diff`/`git status` 確認本次修改僅觸及 `MainFrame.cpp`/`MainFrame.hpp` 兩個檔案，`GLCanvas3D.cpp`/`IMSlider.cpp` 全未變動），此為完全獨立、與 printer technology 無關的既有功能，過去因 `MainFrame.cpp:858` 一律呼叫 `evt.Skip()`、加上匯出對話框搶走焦點而被遮蔽，本次修補讓它得以正常顯現，非本次改動引入或誤動的行為。
- [x] 4.5 切回 FDM 模式後，確認「Export plate sliced file」「Export all plate sliced file」「Export G-code」重新出現時的 enable/disable 狀態（依切片是否完成）與修改前一致。
